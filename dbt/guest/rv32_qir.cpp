#include "dbt/guest/rv32_qir.h"
#include "dbt/guest/rv32_decode.h"
#include "dbt/guest/rv32_runtime.h"
#include "dbt/qjit/qir_printer.h"

#include <sstream>

namespace dbt::qir::rv32
{

namespace GlobalRegId
{
enum {
	GPR_START = 0,
	GPR_END = 31,
	IP = GPR_END,
	END,
};
} // namespace GlobalRegId

static inline VOperand vgpr(u8 id, VType type = VType::I32)
{
	assert(id != 0);
	return VOperand::MakeVGPR(type, GlobalRegId::GPR_START + id - 1);
}

static inline VOperand vconst(u32 val, VType type = VType::I32)
{
	return VOperand::MakeConst(type, val);
}

static inline VOperand vtemp(qir::Builder &qb, VType type = VType::I32)
{
	return VOperand::MakeVGPR(type, qb.CreateVGPR(type));
}

static inline VOperand gprop(u8 id, VType type = VType::I32)
{
	if (!id) {
		return vconst(0, type);
	}
	return vgpr(id, type);
}

StateInfo const *RV32Translator::GetStateInfo()
{
	static std::array<StateReg, GlobalRegId::END> state_regs{};
	static StateInfo state_info{state_regs.data(), state_regs.size()};

	for (u8 i = 1; i < 32; ++i) {
		u16 offs = offsetof(CPUState, gpr) + 4 * i;
		int vreg_no = GlobalRegId::GPR_START + i - 1;
		state_regs[vreg_no] = StateReg{offs, VType::I32, rv32::insn::GRPToName(i)};
	}
	state_regs[GlobalRegId::IP] = StateReg{offsetof(CPUState, ip), VType::I32, "ip"};

	return &state_info;
}
StateInfo const *const RV32Translator::state_info = GetStateInfo();

RV32Translator::RV32Translator(qir::Region *region_) : qb(region_->CreateBlock()) {}

void RV32Translator::Translate(qir::Region *region, u32 ip, u32 boundary_ip)
{
	log_qir("RV32Translator translate [%08x]", ip);
	RV32Translator t(region);
	t.insn_ip = ip;

	u32 num_insns = 0;
	while (true) {
		t.TranslateInsn();
		num_insns++;
		if (t.control != Control::NEXT) {
			break;
		}
		if (num_insns == TB_MAX_INSNS || t.insn_ip == boundary_ip) {
			t.control = Control::TB_OVF;
			t.qb.Create_gbr(VOperand::MakeConst(VType::I32, t.insn_ip));
			break;
		}
	}
	log_qir("RV32Translator translated");
}

void RV32Translator::PreSideeff()
{
	auto offs = state_info->GetStateReg(GlobalRegId::IP)->state_offs;
	auto ip_spill = qir::VOperand::MakeSlot(true, qir::VType::I32, offs);
	qb.Create_mov(ip_spill, vconst(insn_ip, qir::VType::I32));
}

void RV32Translator::TranslateInsn()
{
	auto *insn_ptr = (u32 *)mmu::g2h(insn_ip);

	using decoder = insn::Decoder<RV32Translator>;
	(this->*decoder::Decode(insn_ptr))(insn_ptr);
}

void RV32Translator::TranslateBrcc(rv32::insn::B i, CondCode cc)
{
	auto bb_f = qb.CreateBlock();
	auto bb_t = qb.CreateBlock();
	qb.GetBlock()->AddSucc(bb_t);
	qb.GetBlock()->AddSucc(bb_f);
	qb.Create_brcc(cc, gprop(i.rs1()), gprop(i.rs2()));
	qb = Builder(bb_f);
	qb.Create_gbr(vconst(insn_ip + 4));
	qb = Builder(bb_t);
	qb.Create_gbr(vconst(insn_ip + i.imm()));
}

inline void RV32Translator::TranslateSetcc(rv32::insn::R i, CondCode cc)
{
	if (i.rd()) {
		qb.Create_setcc(cc, vgpr(i.rd()), gprop(i.rs1()), gprop(i.rs2()));
	}
}

inline void RV32Translator::TranslateSetcc(rv32::insn::I i, CondCode cc)
{
	if (i.rd()) {
		qb.Create_setcc(cc, vgpr(i.rd()), gprop(i.rs1()), vconst(i.imm()));
	}
}

void RV32Translator::TranslateLoad(insn::I i, VType type, VSign sgn)
{
	auto tmp = vtemp(qb);

	qb.Create_add(tmp, gprop(i.rs1()), vconst(i.imm())); // constfold
	if (i.rd()) {
		qb.Create_vmload(type, sgn, vgpr(i.rd()), tmp);
	} else {
		qb.Create_vmload(type, sgn, tmp, tmp);
	}
}

void RV32Translator::TranslateStore(insn::S i, VType type, VSign sgn)
{
	auto tmp = vtemp(qb);
	qb.Create_add(tmp, gprop(i.rs1()), vconst(i.imm()));
	qb.Create_vmstore(type, sgn, tmp, gprop(i.rs2(), type));
}

inline void RV32Translator::TranslateHelper(insn::Base i, void *stub)
{
	qb.Create_hcall(stub, vconst(i.raw));
}

#define TRANSLATOR(name)                                                                                     \
	void RV32Translator::H_##name(void *insn)                                                            \
	{                                                                                                    \
		insn::Insn_##name i{*(u32 *)insn};                                                           \
		if (log_qir.enabled()) {                                                                     \
			std::stringstream ss;                                                                \
			ss << i;                                                                             \
			const auto &res = ss.str();                                                          \
			log_qir("      %08x: %-8s    %s", insn_ip, #name, res.c_str());                      \
		}                                                                                            \
		static constexpr auto flags = decltype(i)::flags;                                            \
		if constexpr (flags & insn::Flags::Branch || flags & insn::Flags::Trap ||                    \
			      (flags & insn::Flags::MayTrap && config::unsafe_traps)) {                      \
			PreSideeff();                                                                        \
		}                                                                                            \
		V_##name(i);                                                                                 \
		if constexpr (flags & insn::Flags::Branch || flags & insn::Flags::Trap) {                    \
			control = RV32Translator::Control::BRANCH;                                           \
		}                                                                                            \
		insn_ip += 4;                                                                                \
	}                                                                                                    \
	ALWAYS_INLINE void RV32Translator::V_##name(insn::Insn_##name i)

#define TRANSLATOR_Unimpl(name)                                                                              \
	TRANSLATOR(name)                                                                                     \
	{                                                                                                    \
		log_qir("unimplemented insn " #name);                                                        \
		dbt::Panic();                                                                                \
	}

#define TRANSLATOR_ArithmRI(name, op)                                                                        \
	TRANSLATOR(name)                                                                                     \
	{                                                                                                    \
		if (i.rd()) {                                                                                \
			qb.Create_##op(vgpr(i.rd()), gprop(i.rs1()), vconst(i.imm()));                       \
		}                                                                                            \
	}

#define TRANSLATOR_ArithmRR(name, op)                                                                        \
	TRANSLATOR(name)                                                                                     \
	{                                                                                                    \
		if (i.rd()) {                                                                                \
			qb.Create_##op(vgpr(i.rd()), gprop(i.rs1()), gprop(i.rs2()));                        \
		}                                                                                            \
	}

#define TRANSLATOR_Brcc(name, cc)                                                                            \
	TRANSLATOR(name)                                                                                     \
	{                                                                                                    \
		TranslateBrcc(i, CondCode::cc);                                                              \
	}

#define TRANSLATOR_Setcc(name, cc)                                                                           \
	TRANSLATOR(name)                                                                                     \
	{                                                                                                    \
		TranslateSetcc(i, CondCode::cc);                                                             \
	}

#define TRANSLATOR_Load(name, type, sgn)                                                                     \
	TRANSLATOR(name)                                                                                     \
	{                                                                                                    \
		TranslateLoad(i, VType::type, VSign::sgn);                                                   \
	}

#define TRANSLATOR_Store(name, type, sgn)                                                                    \
	TRANSLATOR(name)                                                                                     \
	{                                                                                                    \
		TranslateStore(i, VType::type, VSign::sgn);                                                  \
	}

#define TRANSLATOR_Helper(name)                                                                              \
	TRANSLATOR(name)                                                                                     \
	{                                                                                                    \
		TranslateHelper(i, (void *)HelperOp_##name);                                                 \
	}

TRANSLATOR_Unimpl(ill);
TRANSLATOR(lui)
{
	if (i.rd()) {
		qb.Create_mov(vgpr(i.rd()), vconst(i.imm()));
	}
}
TRANSLATOR(auipc)
{
	if (i.rd()) {
		qb.Create_mov(vgpr(i.rd()), vconst(i.imm() + insn_ip));
	}
}
TRANSLATOR(jal)
{
	// TODO: check alignment
	if (i.rd()) {
		qb.Create_mov(vgpr(i.rd()), vconst(insn_ip + 4));
	}

	qb.Create_gbr(vconst(insn_ip + i.imm()));
}
TRANSLATOR(jalr)
{
	// TODO: check alignment
	auto tgt = vtemp(qb);

	// constfold
	qb.Create_add(tgt, gprop(i.rs1()), vconst(i.imm()));
	qb.Create_and(tgt, tgt, vconst(~(u32)1));

	if (i.rd()) {
		qb.Create_mov(vgpr(i.rd()), vconst(insn_ip + 4));
	}
	qb.Create_gbrind(tgt);
}
TRANSLATOR_Brcc(beq, EQ);
TRANSLATOR_Brcc(bne, NE);
TRANSLATOR_Brcc(blt, LT);
TRANSLATOR_Brcc(bge, GE);
TRANSLATOR_Brcc(bltu, LTU);
TRANSLATOR_Brcc(bgeu, GEU);
TRANSLATOR_Load(lb, I8, S);
TRANSLATOR_Load(lh, I16, S);
TRANSLATOR_Load(lw, I32, S);
TRANSLATOR_Load(lbu, I8, U);
TRANSLATOR_Load(lhu, I16, U);
TRANSLATOR_Store(sb, I8, U);
TRANSLATOR_Store(sh, I16, U);
TRANSLATOR_Store(sw, I32, U);
TRANSLATOR_ArithmRI(addi, add);
TRANSLATOR_Setcc(slti, LT);
TRANSLATOR_Setcc(sltiu, LTU);
TRANSLATOR_ArithmRI(xori, xor);
TRANSLATOR_ArithmRI(ori, or);
TRANSLATOR_ArithmRI(andi, and);
TRANSLATOR_ArithmRI(slli, sll);
TRANSLATOR_ArithmRI(srai, sra);
TRANSLATOR_ArithmRI(srli, srl);
TRANSLATOR_ArithmRR(sub, sub);
TRANSLATOR_ArithmRR(add, add);
TRANSLATOR_ArithmRR(sll, sll);
TRANSLATOR_Setcc(slt, LT);
TRANSLATOR_Setcc(sltu, LTU);
TRANSLATOR_ArithmRR(xor, xor);
TRANSLATOR_ArithmRR(sra, sra);
TRANSLATOR_ArithmRR(srl, srl);
TRANSLATOR_ArithmRR(or, or);
TRANSLATOR_ArithmRR(and, and);
TRANSLATOR_Helper(fence);
TRANSLATOR_Helper(fencei);
TRANSLATOR_Helper(ecall);
TRANSLATOR_Helper(ebreak);

TRANSLATOR_Helper(lrw);
TRANSLATOR_Helper(scw);
TRANSLATOR_Helper(amoswapw);
TRANSLATOR_Helper(amoaddw);
TRANSLATOR_Helper(amoxorw);
TRANSLATOR_Helper(amoandw);
TRANSLATOR_Helper(amoorw);
TRANSLATOR_Helper(amominw);
TRANSLATOR_Helper(amomaxw);
TRANSLATOR_Helper(amominuw);
TRANSLATOR_Helper(amomaxuw);

} // namespace dbt::qir::rv32

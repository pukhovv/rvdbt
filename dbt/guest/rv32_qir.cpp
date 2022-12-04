#include "dbt/guest/rv32_qir.h"
#include "dbt/guest/rv32_decode.h"
#include "dbt/guest/rv32_runtime.h"
#include "dbt/qjit/qir_printer.h"

#include <sstream>

namespace dbt::qir::rv32
{

RV32Translator::RV32Translator(qir::Region *region_) : qb(region_->CreateBlock())
{
	vgpr[0] = VReg();
	for (u8 i = 1; i < vgpr.size(); ++i) {
		// u16 offs = offsetof(CPUState, gpr) + 4 * i;
		vgpr[i] = VReg(VType::I32, i);
	}
	vreg_ip = VReg(VType::I32, 32);

	temps_idx = 33; // TODO: temps
}

TBlock *RV32Translator::Translate(CPUState *state, u32 ip)
{
	MemArena arena(1024 * 64);
	qir::Region region(&arena);

	RV32Translator t(&region);
	t.insn_ip = ip;

	log_qir("Translate [%08x]", ip);

	u32 upper_bound = -1;
	if (auto *tb_bound = tcache::LookupUpperBound(ip)) {
		upper_bound = tb_bound->ip;
	}

	u32 num_insns = 0;
	while (true) {
		t.TranslateInsn();
		num_insns++;
		if (t.control != Control::NEXT) {
			break;
		}
		if (num_insns == TB_MAX_INSNS || t.insn_ip == upper_bound) {
			t.control = Control::TB_OVF;
			t.qb.Create_gbr(VConst(VType::I32, t.insn_ip));
			break;
		}
	}

	log_qir("Translated", ip);

	PrinterPass printer;
	printer.run(&region);

	return nullptr;
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
	qb.Create_gbr(const32(insn_ip + 4));
	qb = Builder(bb_t);
	qb.Create_gbr(const32(insn_ip + i.imm()));
}

void RV32Translator::TranslateLoad(insn::I i, VType type, VSign sgn)
{
	auto tmp = temp32();

	qb.Create_add(tmp, gprop(i.rs1()), const32(i.imm())); // constfold
	if (i.rd()) {
		qb.Create_vmload(type, sgn, vgpr[i.rd()].WithType(VType::I32), tmp);
	} else {
		qb.Create_vmload(type, sgn, tmp.WithType(VType::I32), tmp);
	}
}

void RV32Translator::TranslateStore(insn::S i, VType type, VSign sgn)
{
	auto tmp = temp32();
	qb.Create_add(tmp, gprop(i.rs1()), const32(i.imm()));
	qb.Create_vmstore(type, sgn, tmp, gprop(i.rs2(), type));
}

inline void RV32Translator::TranslateHelper(insn::Base i, void *stub)
{
	qb.Create_hcall(stub, const32(i.raw));
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
			qb.Create_##op(vgpr[i.rd()], gprop(i.rs1()), const32(i.imm()));                      \
		}                                                                                            \
	}

#define TRANSLATOR_ArithmRR(name, op)                                                                        \
	TRANSLATOR(name)                                                                                     \
	{                                                                                                    \
		if (i.rd()) {                                                                                \
			qb.Create_##op(vgpr[i.rd()], gprop(i.rs1()), gprop(i.rs2()));                        \
		}                                                                                            \
	}

#define TRANSLATOR_Brcc(name, cc)                                                                            \
	TRANSLATOR(name)                                                                                     \
	{                                                                                                    \
		TranslateBrcc(i, CondCode::cc);                                                              \
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

inline VConst RV32Translator::const32(u32 val)
{
	return VConst(VType::I32, val);
}

inline VReg RV32Translator::temp32()
{
	return VReg(VType::I32, temps_idx++);
}

inline VOperand RV32Translator::gprop(u8 idx, VType type)
{
	if (!idx) {
		return VConst(type, 0);
	}
	return vgpr[idx].WithType(type);
}

TRANSLATOR_Unimpl(ill);
TRANSLATOR(lui)
{
	if (i.rd()) {
		qb.Create_mov(vgpr[i.rd()], const32(i.imm()));
	}
}
TRANSLATOR(auipc)
{
	if (i.rd()) {
		qb.Create_mov(vgpr[i.rd()], const32(i.imm() + insn_ip));
	}
}
TRANSLATOR(jal)
{
	// TODO: check alignment
	if (i.rd()) {
		qb.Create_mov(vgpr[i.rd()], const32(insn_ip + 4));
	}

	qb.Create_gbr(VConst(VType::I32, insn_ip + i.imm()));
}
TRANSLATOR(jalr)
{
	// TODO: check alignment
	auto tgt = temp32();

	// constfold
	qb.Create_add(tgt, gprop(i.rs1()), const32(i.imm()));
	qb.Create_and(tgt, tgt, const32(~(u32)1));

	if (i.rd()) {
		qb.Create_mov(vgpr[i.rd()], const32(insn_ip + 4));
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
TRANSLATOR_Unimpl(slti);
TRANSLATOR_Unimpl(sltiu);
TRANSLATOR_ArithmRI(xori, xor);
TRANSLATOR_ArithmRI(ori, or);
TRANSLATOR_ArithmRI(andi, and);
TRANSLATOR_ArithmRI(slli, sll);
TRANSLATOR_ArithmRI(srai, sra);
TRANSLATOR_ArithmRI(srli, srl);
TRANSLATOR_ArithmRR(sub, sub);
TRANSLATOR_ArithmRR(add, add);
TRANSLATOR_ArithmRR(sll, sll);
TRANSLATOR_Unimpl(slt);
TRANSLATOR_Unimpl(sltu);
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

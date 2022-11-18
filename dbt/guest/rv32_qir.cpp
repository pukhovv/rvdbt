#include "dbt/guest/rv32_qir.h"
#include "dbt/guest/rv32_decode.h"
#include "dbt/qjit/qir_printer.h"

#include <sstream>

namespace dbt::qir::rv32
{

RV32Translator::RV32Translator(qir::Region *region_) : qb(region_)
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
			assert(!"must insert branch here");
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

inline VConst RV32Translator::const32(u32 val)
{
	return VConst(VType::I32, val);
}

inline VReg RV32Translator::temp32()
{
	return VReg(VType::I32, temps_idx++);
}

inline VOperandUn RV32Translator::gprop(u8 idx, VType type)
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
TRANSLATOR_Unimpl(beq);
TRANSLATOR(bne)
{
	auto bcc = qb.Create_brcc(CondCode::NE, Label(nullptr), gprop(i.rs1()), gprop(i.rs2()));
	qb.Create_gbr(const32(insn_ip + 4));
	bcc->target = Label(qb.Create_label());
	qb.Create_gbr(const32(insn_ip + i.imm()));
}
TRANSLATOR_Unimpl(blt);
TRANSLATOR_Unimpl(bge);
TRANSLATOR_Unimpl(bltu);
TRANSLATOR_Unimpl(bgeu);
TRANSLATOR_Unimpl(lb);
TRANSLATOR_Unimpl(lh);
TRANSLATOR(lw)
{
	auto tmp = temp32();

	qb.Create_add(tmp, gprop(i.rs1()), const32(i.imm())); // constfold
	if (i.rd()) {
		qb.Create_vmload(vgpr[i.rd()].WithType(VType::I32), tmp);
	} else {
		qb.Create_vmload(tmp.WithType(VType::I32), tmp);
	}
}
TRANSLATOR_Unimpl(lbu);
TRANSLATOR_Unimpl(lhu);
TRANSLATOR_Unimpl(sb);
TRANSLATOR_Unimpl(sh);
TRANSLATOR(sw)
{
	auto tmp = temp32();
	qb.Create_add(tmp, gprop(i.rs1()), const32(i.imm()));
	qb.Create_vmstore(tmp, gprop(i.rs2(), VType::I32));
}
TRANSLATOR_ArithmRI(addi, add);
TRANSLATOR_Unimpl(slti);
TRANSLATOR_Unimpl(sltiu);
TRANSLATOR_Unimpl(xori);
TRANSLATOR_Unimpl(ori);
TRANSLATOR_ArithmRI(andi, and);
TRANSLATOR_ArithmRI(slli, sll);
TRANSLATOR_Unimpl(srai);
TRANSLATOR_Unimpl(srli);
TRANSLATOR_Unimpl(sub);
TRANSLATOR_ArithmRR(add, add);
TRANSLATOR_Unimpl(sll);
TRANSLATOR_Unimpl(slt);
TRANSLATOR_Unimpl(sltu);
TRANSLATOR_Unimpl(xor);
TRANSLATOR_Unimpl(sra);
TRANSLATOR_Unimpl(srl);
TRANSLATOR_Unimpl(or);
TRANSLATOR_Unimpl(and);
TRANSLATOR_Unimpl(fence);
TRANSLATOR_Unimpl(fencei);
TRANSLATOR_Unimpl(ecall);
TRANSLATOR_Unimpl(ebreak);

TRANSLATOR_Unimpl(lrw);
TRANSLATOR_Unimpl(scw);
TRANSLATOR_Unimpl(amoswapw);
TRANSLATOR_Unimpl(amoaddw);
TRANSLATOR_Unimpl(amoxorw);
TRANSLATOR_Unimpl(amoandw);
TRANSLATOR_Unimpl(amoorw);
TRANSLATOR_Unimpl(amominw);
TRANSLATOR_Unimpl(amomaxw);
TRANSLATOR_Unimpl(amominuw);
TRANSLATOR_Unimpl(amomaxuw);

} // namespace dbt::qir::rv32

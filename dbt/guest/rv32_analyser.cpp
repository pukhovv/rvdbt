#include "dbt/guest/rv32_analyser.h"
#include "dbt/guest/rv32_cpu.h"
#include "dbt/guest/rv32_decode.h"
#include "dbt/tcache/cflow_dump.h"

#include <sstream>

namespace dbt::rv32
{
LOG_STREAM(analyse)

RV32Analyser::RV32Analyser(ModuleGraph *mg_, u32 ip, uptr vmem) : vmem_base(vmem), bb_ip(ip), mg(mg_) {}

void RV32Analyser::Analyse(ModuleGraph *mg, u32 ip, u32 boundary_ip, uptr vmem)
{
	log_analyse("RV32Analyser: [%08x:%08x]", ip, boundary_ip);
	RV32Analyser t(mg, ip, vmem);
	t.insn_ip = ip;
	// mg->RecordEntry(ip);

	u32 num_insns = 0;
	while (true) {
		t.AnalyseInsn();
		num_insns++;
		if (t.control != Control::NEXT) {
			break;
		}
		if (num_insns == TB_MAX_INSNS || t.insn_ip >= boundary_ip) {
			t.control = Control::TB_OVF;
			mg->RecordGBr(t.bb_ip, t.insn_ip);
			break;
		}
	}
	mg->GetNode(t.bb_ip)->ip_end = t.insn_ip;
	log_analyse("RV32Analyser: stop at %08x", t.insn_ip);
}

void RV32Analyser::AnalyseInsn()
{
	auto *insn_ptr = (u32 *)(vmem_base + insn_ip);

	using decoder = insn::Decoder<RV32Analyser>;
	(this->*decoder::Decode(insn_ptr))(insn_ptr);
}

void RV32Analyser::AnalyseBrcc(rv32::insn::B i)
{
	mg->RecordGBr(bb_ip, insn_ip + 4);
	mg->RecordGBr(bb_ip, insn_ip + i.imm());
}

template <typename IType>
static ALWAYS_INLINE void LogInsn(IType i, u32 ip)
{
	if (likely(!log_analyse.enabled())) {
		return;
	}
	std::stringstream ss;
	ss << i;
	const auto &res = ss.str();
	log_analyse("\t %08x: %-8s   %s", ip, IType::opcode_str, res.c_str());
}

#define Analyser(name)                                                                                       \
	void RV32Analyser::H_##name(void *insn)                                                              \
	{                                                                                                    \
		insn::Insn_##name i{*(u32 *)insn};                                                           \
		LogInsn(i, insn_ip);                                                                         \
		static constexpr auto flags = decltype(i)::flags;                                            \
		V_##name(i);                                                                                 \
		if constexpr (flags & insn::Flags::Branch || flags & insn::Flags::Trap) {                    \
			control = RV32Analyser::Control::BRANCH;                                             \
		}                                                                                            \
		insn_ip += 4;                                                                                \
	}                                                                                                    \
	ALWAYS_INLINE void RV32Analyser::V_##name(insn::Insn_##name i)

#define Analyser_Unimpl(name)                                                                                \
	Analyser(name)                                                                                       \
	{                                                                                                    \
		log_analyse("unimplemented insn " #name);                                                    \
		dbt::Panic();                                                                                \
	}

Analyser_Unimpl(ill);
Analyser(lui) {}
Analyser(auipc) {}
Analyser(jal)
{
	// TODO: check alignment
	if (i.rd()) {
		mg->RecordGBrLink(bb_ip, insn_ip + i.imm(), insn_ip + 4);
	} else {
		mg->RecordGBr(bb_ip, insn_ip + i.imm());
	}
}
Analyser(jalr)
{
	if (i.rd()) {
		mg->RecordGBrind(bb_ip, insn_ip + 4);
	} else {
		mg->RecordGBrind(bb_ip);
	}
}
Analyser(beq)
{
	AnalyseBrcc(i);
}
Analyser(bne)
{
	AnalyseBrcc(i);
}
Analyser(blt)
{
	AnalyseBrcc(i);
}
Analyser(bge)
{
	AnalyseBrcc(i);
}
Analyser(bltu)
{
	AnalyseBrcc(i);
}
Analyser(bgeu)
{
	AnalyseBrcc(i);
}
Analyser(lb) {}
Analyser(lh) {}
Analyser(lw) {}
Analyser(lbu) {}
Analyser(lhu) {}
Analyser(sb) {}
Analyser(sh) {}
Analyser(sw) {}
Analyser(addi) {}
Analyser(slti) {}
Analyser(sltiu) {}
Analyser(xori) {}
Analyser(ori) {}
Analyser(andi) {}
Analyser(slli) {}
Analyser(srai) {}
Analyser(srli) {}
Analyser(sub) {}
Analyser(add) {}
Analyser(sll) {}
Analyser(slt) {}
Analyser(sltu) {}
Analyser(xor) {}
Analyser(sra) {}
Analyser(srl) {}
Analyser(or) {}
Analyser(and) {}
Analyser(fence) {}
Analyser(fencei) {}
Analyser(ecall) {}
Analyser(ebreak) {}

Analyser(lrw) {}
Analyser(scw) {}
Analyser(amoswapw) {}
Analyser(amoaddw) {}
Analyser(amoxorw) {}
Analyser(amoandw) {}
Analyser(amoorw) {}
Analyser(amominw) {}
Analyser(amomaxw) {}
Analyser(amominuw) {}
Analyser(amomaxuw) {}

} // namespace dbt::rv32

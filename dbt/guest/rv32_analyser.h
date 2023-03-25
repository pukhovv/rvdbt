#pragma once

#include "dbt/aot/aot_module.h"
#include "dbt/guest/rv32_insn.h"

namespace dbt::rv32
{
using namespace dbt::rv32;

struct RV32Analyser {

#define OP(name, format_, flags_)                                                                            \
	void H_##name(void *insn);                                                                           \
	void V_##name(rv32::insn::Insn_##name);                                                              \
	static constexpr auto _##name = &RV32Analyser::H_##name;
	RV32_OPCODE_LIST()
#undef OP

	static void Analyse(ModuleGraph *mg, u32 ip, u32 boundary_ip, uptr vmem);

private:
	explicit RV32Analyser(ModuleGraph *mg_, u32 ip, uptr vmem);
	void AnalyseInsn();
	void AnalyseBrcc(rv32::insn::B i);

	enum class Control { NEXT, BRANCH, TB_OVF } control{Control::NEXT};
	uptr vmem_base{};
	u32 insn_ip{0};
	u32 bb_ip{}; // for cflow_dump

	ModuleGraph *mg{};
};

} // namespace dbt::rv32

namespace dbt
{
using CodeAnalyser = rv32::RV32Analyser;
} // namespace dbt

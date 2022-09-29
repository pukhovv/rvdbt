#pragma once

#include "dbt/guest/rv32_cpu.h"
#include "dbt/guest/rv32_ops.h"

namespace dbt::rv32
{

#define OP(name, format, flags) extern "C" void HelperOp_##name(CPUState *state, u32 insn_raw);
RV32_OPCODE_LIST()
#undef OP

} // namespace dbt::rv32

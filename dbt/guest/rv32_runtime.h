#pragma once

#include "dbt/guest/rv32_cpu.h"
#include "dbt/guest/rv32_ops.h"

namespace dbt::rv32
{

#define OP(name, format_, flags_) extern "C" void HelperOp_##name(CPUState *state, u32 insn_raw);
RV32_OPCODE_LIST()
#undef OP

struct Interpreter {
	static void Execute(CPUState *state);

private:
	Interpreter() = delete;
};

} // namespace dbt::rv32

namespace dbt
{
using Interpreter = rv32::Interpreter;
} // namespace dbt

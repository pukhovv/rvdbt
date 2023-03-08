#pragma once

#include "dbt/guest/rv32_cpu.h"
#include "dbt/guest/rv32_ops.h"

namespace dbt::rv32
{

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

#pragma once

#include "dbt/core.h"

namespace dbt
{

namespace rv32
{
enum class TrapCode : u32 {
	NONE = 0,
	UNALIGNED_IP,
	ILLEGAL_INSN,
	EBREAK,
	ECALL,
};

struct CPUState {
	using gpr_t = u32;
	static constexpr u8 gpr_num = 32;

	bool IsTrapPending()
	{
		return trapno == TrapCode::NONE;
	}

	void DumpTrace();

	std::array<gpr_t, gpr_num> gpr;
	gpr_t ip;
	TrapCode trapno;
};

struct Interpreter {
	static ALWAYS_INLINE void Execute(CPUState *state)
	{
		return _Dispatch(state, state->ip, mmu::base, 0);
	}

	static void _Dispatch(CPUState *state, u32 gip, u8 *vmem, [[maybe_unused]] u32 unused);

private:
	Interpreter() = delete;
};
} // namespace rv32

using CPUState = rv32::CPUState;
using Interpreter = rv32::Interpreter;

} // namespace dbt

#pragma once

#include "dbt/core.h"
#include "dbt/guest/rv32_insn.h"

namespace dbt::rv32
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

} // namespace dbt::rv32

namespace dbt
{
using CPUState = rv32::CPUState;
} // namespace dbt

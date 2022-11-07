#pragma once

#include "dbt/guest/rv32_insn.h"
#include "dbt/mmu.h"

#include <array>

namespace dbt::rv32
{
enum class TrapCode : u32 {
	NONE = 0,
	UNALIGNED_IP,
	ILLEGAL_INSN,
	EBREAK,
	ECALL,
	TERMINATED,
};

struct CPUState {
	using gpr_t = u32;
	static constexpr u8 gpr_num = 32;

	bool IsTrapPending()
	{
		return trapno == TrapCode::NONE;
	}

	void DumpTrace(char const *event);

	std::array<gpr_t, gpr_num> gpr;
	gpr_t ip;
	TrapCode trapno;
};

static constexpr u16 TB_MAX_INSNS = 64; // to synchronize debug tracing

} // namespace dbt::rv32

namespace dbt
{
using CPUState = rv32::CPUState;
} // namespace dbt

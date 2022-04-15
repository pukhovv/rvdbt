#pragma once

#include "dbt/core.h"
#include "dbt/rv32i_ops.h"

namespace dbt::rv32i
{

enum class TrapCode : u32 {
	NONE = 0,
	UNALIGNED_IP,
	ILLEGAL_INSN,
	EBREAK,
	ECALL,
};
struct CPUState {
	u32 gpr[32];
	u32 ip;
	TrapCode trapno;
};

namespace interp
{
void Dispatch(CPUState *state, u32 gip, u8 *vmem, [[maybe_unused]] u32 unused);

ALWAYS_INLINE void Execute(CPUState *state)
{
	return Dispatch(state, state->ip, mmu::base, 0);
}

#define OP(name, format, flags) extern "C" void HelperOp_##name(CPUState *state, u32 insn_raw);
RV32I_OPCODE_LIST()
#undef OP

} // namespace interp
} // namespace dbt::rv32i

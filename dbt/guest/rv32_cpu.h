#pragma once

#include "dbt/guest/rv32_ops.h"
#include "dbt/qmc/runtime_stubs.h"
#include "dbt/tcache/tcache.h"
#include "dbt/util/common.h"

#include <array>

namespace dbt::rv32
{

enum class TrapCode : u32 {
	NONE = 0,
	UNALIGNED_IP,
	ILLEGAL_INSN,
	EBREAK,
	ECALL,
};

// TODO: separate guest part
struct CPUStateImpl {
	bool IsTrapPending()
	{
		return trapno == TrapCode::NONE;
	}

	void DumpTrace(char const *event);

	using gpr_t = u32;
	static constexpr u8 gpr_num = 32;

	std::array<gpr_t, gpr_num> gpr{};
	gpr_t ip{};
	TrapCode trapno{};

	tcache::L1BrindCache *l1_brind_cache{&tcache::l1_brind_cache};
	RuntimeStubTab stub_tab{};

	uptr sp_unwindptr{};
};

// qmc config, also used to synchronize int/jit debug tracing
static constexpr u16 TB_MAX_INSNS = 64;

} // namespace dbt::rv32

namespace dbt
{
struct uthread;
struct CPUState : rv32::CPUStateImpl {
	CPUState() = delete;
	CPUState(uthread *ut_) : ut(ut_) {}

	static void SetCurrent(CPUState *s)
	{
		tls_current = s;
	}

	static CPUState *Current()
	{
		return tls_current;
	}

	uthread *GetUThread() const
	{
		return ut;
	}

private:
	static thread_local CPUState *tls_current;

	uthread *ut{};
};

} // namespace dbt

#pragma once

#include "dbt/guest/rv32_ops.h"
#include "dbt/qjit/runtime_stubs.h"
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
	TERMINATED,
};

// TODO: separate guest part
struct CPUState {
	static inline void SetCurrent(CPUState *s)
	{
		tls_current = s;
	}

	inline CPUState *Current()
	{
		return tls_current;
	}

	bool IsTrapPending()
	{
		return trapno == TrapCode::NONE;
	}

	void DumpTrace(char const *event);

	using gpr_t = u32;
	static constexpr u8 gpr_num = 32;

	std::array<gpr_t, gpr_num> gpr;
	gpr_t ip;
	TrapCode trapno;

	tcache::JMPCache *jmp_cache_brind{&tcache::jmp_cache_brind};
	RuntimeStubTab stub_tab{};

private:
	static thread_local CPUState *tls_current;
};

// qjit config, also used to synchronize int/jit debug tracing
static constexpr u16 TB_MAX_INSNS = 64;

} // namespace dbt::rv32

namespace dbt
{
using CPUState = rv32::CPUState;
} // namespace dbt

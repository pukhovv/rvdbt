#include "dbt/execute.h"
#include "dbt/guest/rv32_runtime.h"
#include "dbt/qmc/compile.h"
#include "dbt/qmc/qcg/jitabi.h"

namespace dbt
{

sigjmp_buf trap_unwind_env;

static inline bool HandleTrap(CPUState *state)
{
	// Currenlty only delegates to ukernel
	return !state->IsTrapPending();
}

struct JITCompilerRuntime final : CompilerRuntime {
	void *AllocateCode(size_t sz, uint align) override
	{
		return tcache::AllocateCode(sz, align);
	}

	bool AllowsRelocation() const override
	{
		return false;
	}

	uptr GetVMemBase() const override
	{
		return (uptr)mmu::base;
	}

	void UpdateIPBoundary(std::pair<u32, u32> &iprange) const override
	{
#ifndef CONFIG_DUMP_TRACE // TODO(tuning): force page boundary
		u32 upper = iprange.second;
		if (auto *tb_upper = tcache::LookupUpperBound(iprange.first)) {
			upper = std::min(upper, tb_upper->ip);
		}
		// upper = std::min(upper, roundup(iprange.first, mmu::PAGE_SIZE));
		iprange.second = upper;
#endif
	}

	void *AnnounceRegion(u32 ip, std::span<u8> const &code) override
	{
		// TODO: concurrent tcache
		auto tb = tcache::AllocateTBlock();
		if (tb == nullptr) {
			Panic();
		}
		tb->ip = ip;
		tb->tcode = TBlock::TCode{code.data(), code.size()};
		tcache::Insert(tb);
		return (void *)tb;
	}
};

void Execute(CPUState *state)
{
	sigsetjmp(dbt::trap_unwind_env, 0);

	jitabi::ppoint::BranchSlot *branch_slot = nullptr;

	while (likely(!HandleTrap(state))) {
		assert(state == CPUState::Current());
		assert(state->gpr[0] == 0);
		if constexpr (config::use_interp) {
			Interpreter::Execute(state);
			continue;
		}

		TBlock *tb = tcache::Lookup(state->ip);
		if (tb == nullptr) {
			auto jrt = JITCompilerRuntime();
			tb = (TBlock *)qir::CompileAt(&jrt, {state->ip, -1});
		}

		if (branch_slot) {
			branch_slot->Link(tb->tcode.ptr);
		} else {
			tcache::CacheBrind(tb);
		}

		branch_slot = jitabi::trampoline_to_jit(state, mmu::base, tb->tcode.ptr);
		if (unlikely(branch_slot)) {
			state->ip = branch_slot->gip;
		}
	}
}

} // namespace dbt

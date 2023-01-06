#include "dbt/execute.h"
#include "dbt/guest/rv32_runtime.h"
#include "dbt/qjit/qcg/jitabi.h"
#include "dbt/qjit/qcompile.h"

namespace dbt
{

sigjmp_buf trap_unwind_env;

static inline bool HandleTrap(CPUState *state)
{
	// Currenlty only delegates to ukernel
	return !state->IsTrapPending();
}

void Execute(CPUState *state)
{
	sigsetjmp(dbt::trap_unwind_env, 0);

	jitabi::ppoint::BranchSlot *branch_slot = nullptr;

	while (likely(!HandleTrap(state))) {
		assert(state->gpr[0] == 0);
		if constexpr (config::use_interp) {
			Interpreter::Execute(state);
			continue;
		}

		TBlock *tb = tcache::Lookup(state->ip);
		if (tb == nullptr) {
			tb = qir::CompileAt(state->ip);
		}

		if (branch_slot) {
			branch_slot->Link(tb->tcode.ptr);
		} else {
			tcache::CacheBrind(tb);
		}

		branch_slot = jitabi::trampoline_host_to_qjit(state, mmu::base, tb->tcode.ptr);
		if (unlikely(branch_slot)) {
			state->ip = branch_slot->gip;
		}
	}
}

} // namespace dbt

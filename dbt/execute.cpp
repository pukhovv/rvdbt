#include "dbt/execute.h"
#include "dbt/core.h"
#include "dbt/guest/rv32_qir.h"
#include "dbt/guest/rv32_qjit.h"
#include "dbt/guest/rv32_runtime.h"

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

	qjit::BranchSlot *branch_slot = nullptr;

	while (!HandleTrap(state)) {
		assert(state->gpr[0] == 0);
		if constexpr (config::use_interp) {
			Interpreter::Execute(state);
			continue;
		}

		TBlock *tb = tcache::Lookup(state->ip);
		if (tb == nullptr) {
			// tb = qjit::rv32::QuickTranslator::Translate(state, state->ip);
			tb = qir::rv32::RV32Translator::Translate(state, state->ip);
			tcache::Insert(tb);
		}

		if (branch_slot) {
			branch_slot->Link(tb->tcode.ptr);
		} else {
			tcache::OnBrind(tb);
		}

		branch_slot = qjit::trampoline_host_to_qjit(state, mmu::base, tb->tcode.ptr);
		if (unlikely(branch_slot)) {
			state->ip = branch_slot->gip;
		}
	}
}

} // namespace dbt

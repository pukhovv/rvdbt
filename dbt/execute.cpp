#include "dbt/execute.h"
#include "dbt/core.h"
#include "dbt/guest/rv32_cpu.h"
#include "dbt/guest/rv32_qjit.h"

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
		TBlock *tb = tcache::Lookup(state->ip);
		if (tb == nullptr) {
			if constexpr (false) {
				Interpreter::Execute(state);
				continue;
			}
			tb = qjit::rv32::QuickTranslator::Translate(state, state->ip);
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

		if constexpr (!decltype(log_bt())::null) {
			state->DumpTrace();
		}
	}
}

} // namespace dbt

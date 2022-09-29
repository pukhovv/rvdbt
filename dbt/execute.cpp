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

	int br_idx = -1;
	TBlock *tb_prev = nullptr;

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

		if (br_idx >= 0) {
			log_cflow() << "B" << tb_prev->ip << "->B" << tb->ip;
			qjit::Codegen::TBLinker::LinkBranch(tb_prev, br_idx, tb);
		}

		auto tptr = TBlock::TaggedPtr(qjit::enter_tcache(state, tb->tcode.ptr));
		if constexpr (!decltype(log_bt())::null) {
			state->DumpTrace();
		}
		br_idx = tptr.getBranchIdx();
		tb_prev = (TBlock *)tptr.getPtr();
	}
}

} // namespace dbt

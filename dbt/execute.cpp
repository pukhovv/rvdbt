#include "dbt/execute.h"
#include "dbt/core.h"
#include "dbt/rv32i_decode.h"
#include "dbt/rv32i_runtime.h"
#include "dbt/translate.h"

namespace dbt
{

sigjmp_buf trap_unwind_env;

static inline bool HandleTrap(rv32i::CPUState *state)
{
	return state->trapno != rv32i::TrapCode::NONE;
}

static void TraceDump(rv32i::CPUState *state)
{
	for (int i = 0; i < 32; ++i) {
		log_trace() << rv32i::insn::GRPToName(i) << "\t " << state->gpr[i];
	}
}

void Execute(rv32i::CPUState *state)
{
	sigsetjmp(dbt::trap_unwind_env, 0);

	int br_idx = -1;
	TBlock *tb_prev = nullptr;

	while (!HandleTrap(state)) {
		assert(state->gpr[0] == 0);
		TBlock *tb = tcache::Lookup(state->ip);
		if (tb == nullptr) {
			if constexpr (false) {
				rv32i::interp::Execute(state);
				continue;
			}
			tb = translator::Translate(state, state->ip);
			tcache::Insert(tb);
		}

		if (br_idx >= 0) {
			log_cflow() << "B" << tb_prev->ip << "->B" << tb->ip;
			translator::Codegen::TBLinker::LinkBranch(tb_prev, br_idx, tb);
		}

		auto tptr = TBlock::TaggedPtr(dbt::enter_tcache(state, tb->tcode.ptr));
		if constexpr (!decltype(log_bt())::null) {
			TraceDump(state);
		}
		br_idx = tptr.getBranchIdx();
		tb_prev = (TBlock *)tptr.getPtr();
	}
}

} // namespace dbt

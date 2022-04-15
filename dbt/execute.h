#pragma once

#include "dbt/core.h"
#include "dbt/rv32i_runtime.h"
#include "dbt/translate.h"

namespace dbt
{

extern sigjmp_buf trap_unwind_env;

ALWAYS_INLINE void RaiseTrap()
{
	siglongjmp(trap_unwind_env, 1);
}

void Execute(rv32i::CPUState *state);

} // namespace dbt

#include "dbt/execute.h"
#include "dbt/qmc/qcg/arch_traits.h"
#include "dbt/qmc/qcg/jitabi.h"
#include "dbt/tcache/tcache.h"

namespace dbt::jitabi
{

struct _RetPair {
	void *v0;
	void *v1;
};

#define HELPER extern "C" NOINLINE __attribute__((used))
#define HELPER_ASM extern "C" NOINLINE __attribute__((used, naked))

HELPER void qcgstub_nevercalled(CPUState *state, void *membase)
{
	Panic("\"nevercalled\" stub called!");
}

} // namespace dbt::jitabi

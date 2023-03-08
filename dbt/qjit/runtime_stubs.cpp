#include "dbt/qjit/runtime_stubs.h"

namespace dbt
{

#define X(name) extern "C" void qcgstub_##name();
RUNTIME_STUBS(X)
#undef X

RuntimeStubTab RuntimeStubTab::Create()
{
	table_t tab;
#define X(name) tab[to_underlying(RuntimeStubId::id_##name)] = reinterpret_cast<uptr>(qcgstub_##name);
	RUNTIME_STUBS(X)
#undef X
	return tab;
}

const RuntimeStubTab RuntimeStubTab::g_tab = RuntimeStubTab::Create();

} // namespace dbt

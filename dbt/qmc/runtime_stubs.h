#pragma once

#include "dbt/guest/rv32_stubs.h"
#include "dbt/util/common.h"

#include <array>

namespace dbt
{
#define COMMON_RUNTIME_STUBS(X)                                                                              \
	X(escape_link)                                                                                       \
	X(escape_brind)                                                                                      \
	X(link_branch_jit)                                                                                   \
	X(link_branch_aot)                                                                                   \
	X(link_branch_llvmaot)                                                                               \
	X(brind)                                                                                             \
	X(raise)                                                                                             \
	X(trace)                                                                                             \
	X(nevercalled)

#define RUNTIME_STUBS(X) COMMON_RUNTIME_STUBS(X) GUEST_RUNTIME_STUBS(X)

enum class RuntimeStubId {
#define X(name) id_##name,
	RUNTIME_STUBS(X)
#undef X
	    Count,
};

struct RuntimeStubTab {
	RuntimeStubTab() : data(g_tab.data) {}

	static RuntimeStubTab const *GetGlobal()
	{
		return &g_tab;
	}

	uptr operator[](RuntimeStubId id) const
	{
		return data[to_underlying(id)];
	}

	static constexpr uptr offs(RuntimeStubId id)
	{
		return sizeof(uptr) * to_underlying(id);
	}

private:
	using table_t = std::array<uptr, to_underlying(RuntimeStubId::Count)>;

	static const RuntimeStubTab g_tab;
	RuntimeStubTab(table_t data_) : data(data_) {}
	static RuntimeStubTab Create();

	const table_t data;
};

} // namespace dbt

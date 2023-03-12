#pragma once

#include "dbt/util/common.h"
#include <span>

namespace dbt
{
struct CompilerRuntime {
	virtual void *AllocateCode(size_t sz, uint align) = 0;

	virtual bool AllowsRelocation() const = 0;

	virtual uptr GetVMemBase() const = 0;

	virtual void UpdateIPBoundary(std::pair<u32, u32> &iprange) const = 0;

	virtual void *AnnounceRegion(u32 ip, std::span<u8> const &code) = 0;
};
} // namespace dbt

namespace dbt::qir
{

void *CompileAt(CompilerRuntime *cruntime, std::pair<u32, u32> iprange);

} // namespace dbt::qir

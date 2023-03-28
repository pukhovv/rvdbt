#pragma once

#include "dbt/util/common.h"
#include <span>
#include <vector>

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

struct CodeSegment {
	explicit CodeSegment(u32 gip_base_, u32 size_) : gip_base(gip_base_), size(size_) {}

	bool InSegment(u32 gip) const
	{
		return (gip - gip_base) < size;
	}

	u32 gip_base;
	u32 size;
};

struct CompilerJob {
	using IpRangesSet = std::vector<std::pair<u32, u32>>;

	explicit CompilerJob(CompilerRuntime *cruntime_, CodeSegment segment_, IpRangesSet &&iprange_)
	    : cruntime(cruntime_), segment(segment_), iprange(iprange_)
	{
		assert(iprange.size());
	}

	CompilerRuntime *cruntime;
	CodeSegment segment;
	IpRangesSet iprange;
};

void *CompilerDoJob(CompilerJob &job);

} // namespace dbt::qir

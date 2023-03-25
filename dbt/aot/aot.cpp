#include "dbt/aot/aot.h"
#include "dbt/aot/aot_module.h"
#include "dbt/guest/rv32_analyser.h"
#include "dbt/qmc/compile.h"
#include "dbt/tcache/objprof.h"
#include <vector>

namespace dbt
{
LOG_STREAM(aot)

using FilePageData = objprof::PageData;

static ModuleGraph BuildModuleGraph(FilePageData const &page)
{
	u32 const page_vaddr = page.pageno << mmu::PAGE_BITS;
	u32 const next_page_vaddr = page_vaddr + mmu::PAGE_SIZE;

	std::vector<u32> iplist;
	ModuleGraph mg(qir::CodeSegment(page_vaddr, mmu::PAGE_SIZE));

	for (u32 idx = 0; idx < page.executed.size(); ++idx) {
		if (!page.executed[idx]) {
			continue;
		}
		u32 ip = page_vaddr + FilePageData::idx2po(idx);
		iplist.push_back(ip);
		mg.RecordEntry(ip);
		if (page.brind_target[idx]) {
			mg.RecordBrindTarget(ip);
		}
		if (page.segment_entry[idx]) {
			mg.RecordSegmentEntry(ip);
		}
	}

	for (size_t idx = 0; idx < iplist.size(); ++idx) {
		u32 ip = iplist[idx];
		u32 ip_next = (idx == iplist.size() - 1) ? next_page_vaddr : iplist[idx + 1];

		rv32::RV32Analyser::Analyse(&mg, ip, ip_next, (uptr)mmu::base);
	}

	return mg;
}

static void AOTCompilePage(CompilerRuntime *aotrt, FilePageData const &page)
{
	auto mg = BuildModuleGraph(page);
	auto regions = mg.ComputeRegions();

#if 1
	for (auto const &r : regions) {
		assert(r[0]->flags.region_entry);
		qir::CompilerJob::IpRangesSet ipranges;
		for (auto n : r) {
			ipranges.push_back({n->ip, n->ip_end});
		}

		qir::CompilerJob job(aotrt, mg.segment, std::move(ipranges));
		qir::CompilerDoJob(job);
	}
#else
	for (auto const &e : mg.ip_map) {
		auto const &n = *e.second;
		qir::CompilerJob job(aotrt, mg.segment, {{n.ip, n.ip_end}});
		qir::CompilerDoJob(job);
	}
#endif
}

void AOTCompileObject(CompilerRuntime *aotrt)
{
	for (auto const &page : objprof::GetProfile()) {
		AOTCompilePage(aotrt, page);
	}
}

} // namespace dbt

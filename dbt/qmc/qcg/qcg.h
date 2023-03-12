#pragma once

#include "dbt/qmc/compile.h"
#include "dbt/qmc/qir.h"

namespace dbt::qcg
{

LOG_STREAM(qcg);

std::span<u8> GenerateCode(CompilerRuntime *cruntime, qir::Region *r, u32 ip);

struct MachineRegionInfo {
	bool has_calls = false;
};

struct QSelPass {
	static void run(qir::Region *region, MachineRegionInfo *region_info);
};

struct QRegAllocPass {
	static void run(qir::Region *region);
};

}; // namespace dbt::qcg

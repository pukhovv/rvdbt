#pragma once

#include "dbt/qjit/qir.h"
#include "dbt/tcache/tcache.h"

namespace dbt::qcg
{

TBlock::TCode JITGenerate(qir::Region *r, u32 ip);

struct QSelPass {
	static void run(qir::Region *region);
};

struct QRegAllocPass {
	static void run(qir::Region *region);
};

}; // namespace dbt::qcg

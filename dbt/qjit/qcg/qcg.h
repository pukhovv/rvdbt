#pragma once

#include "dbt/qjit/qir.h"
#include "dbt/tcache/tcache.h"

namespace dbt::qcg
{

TBlock::TCode Generate(qir::Region *r, u32 ip);

}; // namespace dbt::qcg

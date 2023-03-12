#pragma once

#include "dbt/qmc/qir.h"

namespace dbt::qir
{

Inst *ApplyFolder(Block *bb, Inst *ins);

} // namespace dbt::qir

#pragma once

#include "dbt/qjit/qir.h"

namespace dbt::qir
{

Inst *ApplyFolder(Block *bb, Inst *ins);

} // namespace dbt::qir

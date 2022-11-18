#pragma once

#include "dbt/qjit/qir.h"

namespace dbt::qir
{

struct PrinterPass {
	void run(Region *r);
};

extern char const *const op_names[to_underlying(Op::Count)];
extern char const *const vtype_names[to_underlying(VType::Count)];
extern char const *const condcode_names[to_underlying(CondCode::Count)];

inline char const *GetOpNameStr(Op op)
{
	return op_names[to_underlying(op)];
}

inline char const *GetVTypeNameStr(VType type)
{
	return vtype_names[to_underlying(type)];
}

inline char const *GetCondCodeNameStr(CondCode cc)
{
	return condcode_names[to_underlying(cc)];
}

} // namespace dbt::qir

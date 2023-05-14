#pragma once

#include "dbt/qmc/qir.h"

namespace dbt::qir
{

struct PrinterPass {
	static std::string run(Region *r);

	static void run(LogStream &stream, std::string const &header, Region *r)
	{
		if (!stream.enabled()) {
			return;
		}
		auto str = qir::PrinterPass::run(r);
		stream.write(header.c_str());
		stream.write(str.c_str());
	}

private:
	PrinterPass() = delete;
};

extern char const *const op_names[to_underlying(Op::Count)];
extern char const *const vtype_names[to_underlying(VType::Count)];
extern char const *const condcode_names[to_underlying(CondCode::Count)];
extern char const *const runtime_stub_names[to_underlying(RuntimeStubId::Count)];

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

inline char const *GetRuntimeStubName(RuntimeStubId id)
{
	return runtime_stub_names[to_underlying(id)];
}

} // namespace dbt::qir

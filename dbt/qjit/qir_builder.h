#pragma once

#include "dbt/qjit/qir.h"
#include "dbt/qjit/qir_opt.h"

namespace dbt::qir
{

struct Builder {
	explicit Builder(Block *bb_) : bb(bb_), it(bb->ilist.end()) {}
	Builder(Block *bb_, IListIterator<Inst> it_) : bb(bb_), it(it_) {}

	auto GetIterator() const
	{
		return it;
	}

	Block *GetBlock() const
	{
		return bb;
	}

	Block *CreateBlock() const
	{
		return bb->GetRegion()->CreateBlock();
	}

	RegN CreateVGPR(VType type) const
	{
		return bb->GetRegion()->GetVRegsInfo()->AddLocal(type);
	}

	template <typename T, typename... Args>
	requires std::is_base_of_v<Inst, T> Inst *Create(Args &&...args)
	{
		auto *ins = bb->GetRegion()->Create<T>(std::forward<Args>(args)...);
		bb->ilist.insert(it, *ins);
		return ApplyFolder(bb, ins);
	}

#define OP(name, cls)                                                                                        \
	template <typename... Args>                                                                          \
	Inst *Create_##name(Args &&...args)                                                                  \
	{                                                                                                    \
		return Create<cls>(Op::_##name, std::forward<Args>(args)...);                                \
	}
	QIR_SUBOPS_LIST(OP)
#undef OP

#define OP(name, cls)                                                                                        \
	template <typename... Args>                                                                          \
	Inst *Create_##name(Args &&...args)                                                                  \
	{                                                                                                    \
		return Create<cls>(std::forward<Args>(args)...);                                             \
	}
	QIR_CLSOPS_LIST(OP)
#undef OP

#define GROUP(cls, beg, end)                                                                                 \
	template <typename... Args>                                                                          \
	Inst *Create##cls(Args &&...args)                                                                    \
	{                                                                                                    \
		return Create<cls>(std::forward<Args>(args)...);                                             \
	}
	QIR_GROUPS_LIST(GROUP)
#undef GROUP

private:
	Block *bb;
	IListIterator<Inst> it;
};

} // namespace dbt::qir

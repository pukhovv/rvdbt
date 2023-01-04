#include "dbt/qjit/qir_opt.h"
#include "dbt/qjit/qir_builder.h"

namespace dbt::qir
{

struct FolderVisitor : qir::InstVisitor<FolderVisitor, bool> {
	using Base = qir::InstVisitor<FolderVisitor, void>;

	FolderVisitor(Block *bb_, Inst *ins_) : qb(bb_, ins_->getIter()) {}

	inline Inst *Apply()
	{
		auto ins = &*qb.GetIterator();
		if (likely(!visit(ins))) {
			return ins;
		}
		auto last_ins = &*--qb.GetIterator();
		qb.GetBlock()->ilist.erase(ins);
		return last_ins;
	}

	inline bool visitInst(Inst *ins)
	{
		return false;
	}

	bool visit_add(InstBinop *ins)
	{
		auto &i = ins->i;
		if (i[0].IsConst()) {
			if (i[1].IsConst()) {
				u32 val = i[0].GetConst() + i[1].GetConst();
				auto opr = VOperand::MakeConst(VType::I32, val);
				qb.Create_mov(ins->o[0], opr);
				return true;
			}
			std::swap(ins->i[0], ins->i[1]);
		}
		if (i[1].IsConst() && i[1].GetConst() == 0) {
			qb.Create_mov(ins->o[0], ins->i[0]);
			return true;
		}
		return false;
	}

private:
	Builder qb;
};

Inst *ApplyFolder(Block *bb, Inst *ins)
{
	return FolderVisitor(bb, ins).Apply();
}

} // namespace dbt::qir

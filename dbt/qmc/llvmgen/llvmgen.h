#include "dbt/qmc/compile.h"
#include "dbt/qmc/qir_builder.h"

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"

namespace dbt::qir
{

struct LLVMGen {
	LLVMGen(llvm::LLVMContext *context_, llvm::Module *cmodule_, CodeSegment *segment_);

	void AddFunction(u32 region_ip);

	llvm::Function *Run(qir::Region *region, u32 region_ip);

private:
#define OP(name, cls, flags) void Emit_##name(qir::cls *ins);
	QIR_OPS_LIST(OP)
#undef OP

	// TODO: make generic
	struct Visitor : qir::InstVisitor<Visitor, void> {
	public:
		Visitor(LLVMGen *cg_) : cg(cg_) {}

		void visitInst(qir::Inst *ins)
		{
			unreachable("");
		}

#define OP(name, cls, flags)                                                                                 \
	void visit_##name(qir::cls *ins)                                                                     \
	{                                                                                                    \
		cg->Emit_##name(ins);                                                                        \
	}
		QIR_OPS_LIST(OP)
#undef OP

	private:
		LLVMGen *cg{};
	};

	void CreateVGPRLocs(qir::VRegsInfo *vinfo);
	llvm::Type *MakeType(VType type);
	llvm::Type *MakePtrType(VType type);
	llvm::Value *LoadVOperand(qir::VOperand op);
	void StoreVOperand(qir::VOperand op, llvm::Value *val);
	llvm::Value *MakeVMemLoc(VType type, llvm::Value *offs);
	llvm::Value *MakeStateEP(VType type, u32 offs);
	llvm::Value *MakeVOperandEP(VOperand op);
	llvm::ConstantInt *MakeConst(VType type, u64 val);

	static llvm::CmpInst::Predicate MakeCC(CondCode cc);
	llvm::Value *MakeRStub(RuntimeStubId id, llvm::FunctionType *ftype);

	llvm::BasicBlock *MapBB(Block *bb);

	template <u8 Bits>
	llvm::ConstantInt *constv(u64 val)
	{
		return llvm::ConstantInt::get(*ctx, llvm::APInt(Bits, val));
	}

	void EmitBinop(llvm::Instruction::BinaryOps opc, qir::InstBinop *ins);
	void EmitTrace();

	// globals
	llvm::LLVMContext *ctx{};
	llvm::Module *cmodule{};
	llvm::FunctionType *qcg_ftype{}, *qcg_helper_ftype{}, *qcg_brind_ftype{};

	// per segment
	CodeSegment *segment{};

	// translation
	llvm::Function *func{};
	llvm::IRBuilder<> *lb{};
	Block *qbb{};
	std::unordered_map<u32, llvm::BasicBlock *> id2bb;
	llvm::Value *statev{};
	llvm::Value *membasev{};
	llvm::Value *spunwindv{};
	std::vector<llvm::Value *> vlocs;
};

} // namespace dbt::qir

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
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Type.h"

namespace dbt::qir
{

extern thread_local llvm::LLVMContext g_llvm_ctx;

struct LLVMGen;

struct LLVMGenCtx {
	explicit LLVMGenCtx(llvm::Module *cmodule_);

	llvm::LLVMContext &ctx;
	llvm::Module &cmodule;

	llvm::FunctionType *qcg_fnty{};
	llvm::FunctionType *qcg_gbr_patch_fnty{};
	llvm::FunctionType *qcg_stub_brind_fnty{};
	llvm::FunctionType *qcg_helper_fnty{};
	llvm::StructType *brind_cache_entry_ty{};

	llvm::MDNode *md_unlikely{};
	llvm::MDNode *md_astate{};
	llvm::MDNode *md_avmem{};
	llvm::MDNode *md_aother{};

	std::unordered_map<std::string, CodeSegment> fn2seg;
	std::unordered_map<std::string_view, std::function<bool(LLVMGen &, llvm::CallInst *, bool)>>
	    intrin_fns;

	void AddFunction(u32 region_ip, CodeSegment segment);
};

struct IntrinsicExpansionPass : public llvm::PassInfoMixin<IntrinsicExpansionPass> {

	explicit IntrinsicExpansionPass(LLVMGenCtx &ctx_, bool is_final_) : ctx(ctx_), is_final(is_final_) {}

	llvm::PreservedAnalyses run(llvm::Function &fn, llvm::FunctionAnalysisManager &fam);

private:
	LLVMGenCtx &ctx;
	bool is_final{};
};

struct LLVMGen {
	explicit LLVMGen(LLVMGenCtx &ctx_, llvm::Function *func_);

	llvm::Instruction *AScopeState(llvm::Instruction *inst);
	llvm::Instruction *AScopeVMem(llvm::Instruction *inst);
	llvm::Instruction *AScopeOther(llvm::Instruction *inst);
	llvm::Value *MakeStateEP(llvm::PointerType *type, u32 offs);
	llvm::Value *MakeVMemLoc(llvm::PointerType *ptype, llvm::Value *addr);
	llvm::Value *MakeRStub(RuntimeStubId id, llvm::FunctionType *ftype);

	template <u8 Bits>
	llvm::ConstantInt *constv(u64 val)
	{
		return llvm::ConstantInt::get(lctx, llvm::APInt(Bits, val));
	}

	void CreateQCGFnCall(llvm::Value *fn);
	void CreateQCGGbr(u32 gipv);

	void ExpandIntrinsics(bool is_final);

	LLVMGenCtx &g;
	llvm::LLVMContext &lctx;
	llvm::Module &cmodule;

	CodeSegment *segment;

	llvm::IRBuilder<> *lb{};
	llvm::Function *func{};
	llvm::Value *statev{};
	llvm::Value *membasev{};
};

struct QIRToLLVM : public LLVMGen {
	explicit QIRToLLVM(LLVMGenCtx &ctx_, CodeSegment *segment_, qir::Region *region, u32 region_ip);

	llvm::Function *Run();

private:
#define OP(name, cls, flags) void Emit_##name(qir::cls *ins);
	QIR_OPS_LIST(OP)
#undef OP

	// TODO: make generic
	struct Visitor : qir::InstVisitor<Visitor, void> {
	public:
		Visitor(QIRToLLVM *cg_) : cg(cg_) {}

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
		QIRToLLVM *cg{};
	};

	void CreateVGPRLocs(qir::VRegsInfo *vinfo);
	llvm::Type *MakeType(VType type);
	llvm::PointerType *MakePtrType(VType type);

	llvm::Value *LoadVOperand(qir::VOperand op);
	void StoreVOperand(qir::VOperand op, llvm::Value *val);
	llvm::Value *MakeVMemLoc(VType type, llvm::Value *addr);
	llvm::Value *MakeStateEP(VType type, u32 offs);
	std::pair<llvm::Value *, bool> MakeVOperandEP(VOperand op);
	llvm::ConstantInt *MakeConst(VType type, u64 val);

	static llvm::CmpInst::Predicate MakeCC(CondCode cc);

	llvm::BasicBlock *MapBB(Block *bb);

	void EmitBinop(llvm::Instruction::BinaryOps opc, qir::InstBinop *ins);
	void EmitTrace();

	qir::Region *region;
	Block *qbb{};
	std::unordered_map<u32, llvm::BasicBlock *> id2bb;
	std::vector<llvm::Value *> vlocs;
	RegN vlocs_nglobals{};
};

} // namespace dbt::qir

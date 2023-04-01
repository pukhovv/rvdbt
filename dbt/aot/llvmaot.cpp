#include "dbt/aot/aot.h"
#include "dbt/qmc/compile.h"
#include "dbt/qmc/qcg/jitabi.h"
#include "dbt/qmc/qir_builder.h"
#include "dbt/tcache/objprof.h"

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetIntrinsicInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"

#include <memory>
#include <optional>
#include <sstream>

namespace dbt
{
LOG_STREAM(aot)

namespace qir
{

struct LLVMGen {
	LLVMGen(qir::Region *region_, u32 ip_, llvm::LLVMContext *ctx_, llvm::Module *cmodule_)
	    : region(region_), region_ip(ip_), ctx(ctx_), cmodule(cmodule_)
	{
		lb = std::make_unique<llvm::IRBuilder<>>(*ctx);
		vinfo = region->GetVRegsInfo();
	}
	llvm::Function *Run();

#define OP(name, cls, flags) void Emit_##name(qir::cls *ins);
	QIR_OPS_LIST(OP)
#undef OP

private:
	void CreateVGPRLocs();
	llvm::Type *MakeType(VType type);
	llvm::Type *MakePtrType(VType type);
	llvm::Value *LoadVOperand(qir::VOperand op);
	void StoreVOperand(qir::VOperand op, llvm::Value *val);
	llvm::Value *MakeVMemLoc(VType type, llvm::Value *offs);

	static llvm::CmpInst::Predicate MakeCC(CondCode cc);

	llvm::BasicBlock *MapBB(Block *bb);

	llvm::ConstantInt *const32(u32 val)
	{
		return llvm::ConstantInt::get(*ctx, llvm::APInt(32, val));
	}

	llvm::ConstantInt *const64(u64 val)
	{
		return llvm::ConstantInt::get(*ctx, llvm::APInt(64, val));
	}

	qir::Region *region{};
	qir::VRegsInfo *vinfo{};
	u32 region_ip{};

	Block *qbb{};

	llvm::LLVMContext *ctx{};
	llvm::Module *cmodule{};
	llvm::Function *func{};
	std::unique_ptr<llvm::IRBuilder<>> lb;

	std::unordered_map<u32, llvm::BasicBlock *> id2bb;

	llvm::Value *statev{};
	llvm::Value *membasev{};
	std::vector<llvm::Value *> vlocs;

	llvm::Function *qcg_jitabi_nevercalled{};
	llvm::ConstantPointerNull *qcgfn_null{};
	u32 stackmap_id{1};
};

llvm::Type *LLVMGen::MakeType(VType type)
{
	switch (type) {
	case VType::I8:
		return llvm::Type::getInt8Ty(*ctx);
	case VType::I16:
		return llvm::Type::getInt16Ty(*ctx);
	case VType::I32:
		return llvm::Type::getInt32Ty(*ctx);
	default:
		unreachable("");
	}
}

llvm::Type *LLVMGen::MakePtrType(VType type)
{
	switch (type) {
	case VType::I8:
		return llvm::Type::getInt8PtrTy(*ctx);
	case VType::I16:
		return llvm::Type::getInt16PtrTy(*ctx);
	case VType::I32:
		return llvm::Type::getInt32PtrTy(*ctx);
	default:
		unreachable("");
	}
}

void LLVMGen::CreateVGPRLocs()
{
	auto n_glob = vinfo->NumGlobals();

	for (RegN i = 0; i < n_glob; ++i) {
		auto *info = vinfo->GetGlobalInfo(i);
		auto name = std::string("@") + info->name;
		auto state_ep =
		    lb->CreateConstInBoundsGEP1_32(MakeType(info->type), statev, info->state_offs, name);
		vlocs.push_back(state_ep);
	}

	for (RegN i = n_glob; i < vinfo->NumAll(); ++i) {
		auto type = vinfo->GetLocalType(i);
		auto name = "%" + std::to_string(i);
		auto state_ep = lb->CreateAlloca(MakeType(type), const32(VTypeToSize(type)), name);
		vlocs.push_back(state_ep);
	}
}

llvm::Value *LLVMGen::LoadVOperand(qir::VOperand op)
{
	auto type = op.GetType();
	if (op.IsConst()) {
		return const32(op.GetConst());
	}
	llvm::Value *state_ep;
	if (op.IsVGPR()) {
		state_ep = vlocs[op.GetVGPR()];
	} else if (op.IsGSlot()) {
		state_ep = lb->CreateConstInBoundsGEP1_32(MakeType(type), statev, op.GetSlotOffs());
	} else {
		unreachable("");
	}
	return lb->CreateAlignedLoad(MakeType(type), state_ep, llvm::Align(VTypeToSize(type)));
}

void LLVMGen::StoreVOperand(qir::VOperand op, llvm::Value *val)
{
	auto type = op.GetType();
	llvm::Value *state_ep;
	// TODO: reuse
	if (op.IsVGPR()) {
		state_ep = vlocs[op.GetVGPR()];
	} else if (op.IsGSlot()) {
		state_ep = lb->CreateConstInBoundsGEP1_32(MakeType(type), statev, op.GetSlotOffs());
	} else {
		unreachable("");
	}
	lb->CreateAlignedStore(val, state_ep, llvm::Align(VTypeToSize(type)));
}

llvm::Value *LLVMGen::MakeVMemLoc(VType type, llvm::Value *offs)
{
	return lb->CreateInBoundsGEP(MakeType(type), membasev, offs);
}

llvm::CmpInst::Predicate LLVMGen::MakeCC(CondCode cc)
{
	switch (cc) {
	case CondCode::EQ:
		return llvm::CmpInst::Predicate::ICMP_EQ;
	case CondCode::NE:
		return llvm::CmpInst::Predicate::ICMP_NE;
	case CondCode::LE:
		return llvm::CmpInst::Predicate::ICMP_SLE;
	case CondCode::LT:
		return llvm::CmpInst::Predicate::ICMP_SLT;
	case CondCode::GE:
		return llvm::CmpInst::Predicate::ICMP_SGE;
	case CondCode::GT:
		return llvm::CmpInst::Predicate::ICMP_SGT;
	case CondCode::LEU:
		return llvm::CmpInst::Predicate::ICMP_ULE;
	case CondCode::LTU:
		return llvm::CmpInst::Predicate::ICMP_ULT;
	case CondCode::GEU:
		return llvm::CmpInst::Predicate::ICMP_UGE;
	case CondCode::GTU:
		return llvm::CmpInst::Predicate::ICMP_UGT;
	default:
		unreachable("");
	}
}

llvm::BasicBlock *LLVMGen::MapBB(Block *bb)
{
	return id2bb.find(bb->GetId())->second;
}

void LLVMGen::Emit_hcall(qir::InstHcall *ins)
{
	unreachable("");
}
void LLVMGen::Emit_br(qir::InstBr *ins)
{
	unreachable("");
}
void LLVMGen::Emit_brcc(qir::InstBrcc *ins)
{
	auto lhs = LoadVOperand(ins->i(0));
	auto rhs = LoadVOperand(ins->i(1));
	auto cmp = lb->CreateCmp(MakeCC(ins->cc), lhs, rhs);

	auto sit = qbb->GetSuccs().begin();
	auto &qbb_t = **sit;
	auto &qbb_f = **++sit;

	lb->CreateCondBr(cmp, MapBB(&qbb_t), MapBB(&qbb_f));
}
void LLVMGen::Emit_gbr(qir::InstGBr *ins)
{
	// llvm doesn't support tailcalls for experimental.patchpoint, actual call is not expanded until
	// MCInstLowering. The same applies to gc.statepoint
	// TODO: Seems like I can implelemnt custom MCInst to replace desired tailcalls like XRay MFPass does
#if 0
	std::array<llvm::Value *, 6> args = {
	    const64(stackmap_id++), const32(sizeof(jitabi::ppoint::BranchSlot)),
	    llvm::ConstantPointerNull::get(llvm::Type::getInt8PtrTy(*ctx)),
	const32(2), statev, membasev};

	auto intr = lb->CreateIntrinsic(llvm::Intrinsic::experimental_patchpoint_void, {}, args);
	intr->addFnAttr(llvm::Attribute::NoReturn);
	intr->setCallingConv(llvm::CallingConv::GHC);
	intr->setTailCall(true);
	intr->setTailCallKind(llvm::CallInst::TCK_MustTail);
	lb->CreateRetVoid();
#else
	llvm::FunctionType *qcg_ftype =
	    llvm::FunctionType::get(llvm::Type::getVoidTy(*ctx),
				    {llvm::Type::getInt8PtrTy(*ctx), llvm::Type::getInt8PtrTy(*ctx)}, false);
	auto *qcgfn_null = llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(qcg_ftype));
	lb->CreateIntrinsic(llvm::Intrinsic::experimental_stackmap, {},
			    {const64(stackmap_id++), const32(sizeof(jitabi::ppoint::BranchSlot))});
	auto call = lb->CreateCall(qcg_ftype, qcgfn_null, {statev, membasev});
	call->addFnAttr(llvm::Attribute::NoReturn);
	call->setCallingConv(llvm::CallingConv::GHC);
	call->setTailCall(true);
	call->setTailCallKind(llvm::CallInst::TCK_MustTail);
	lb->CreateRetVoid();
#endif
}
void LLVMGen::Emit_gbrind(qir::InstGBrind *ins)
{
	unreachable("");
}
void LLVMGen::Emit_vmload(qir::InstVMLoad *ins)
{
	// TODO: resolve types mess
	auto mem_ep = MakeVMemLoc(ins->sz, LoadVOperand(ins->i(0)));
	auto val = lb->CreateAlignedLoad(MakeType(ins->sz), mem_ep, llvm::Align(VTypeToSize(ins->sz)));
	if (ins->o(0).GetType() != ins->sz) {
		unreachable("");
	}
	StoreVOperand(ins->o(0), val);
}
void LLVMGen::Emit_vmstore(qir::InstVMStore *ins)
{
	auto mem_ep = MakeVMemLoc(ins->sz, LoadVOperand(ins->i(0)));
	auto val = LoadVOperand(ins->i(1));
	if (ins->i(1).GetType() != ins->sz) {
		unreachable("");
	}
	lb->CreateAlignedStore(val, mem_ep, llvm::Align(VTypeToSize(ins->sz)));
}
void LLVMGen::Emit_setcc(qir::InstSetcc *ins)
{
	unreachable("");
}
void LLVMGen::Emit_mov(qir::InstUnop *ins)
{
	auto val = LoadVOperand(ins->i(0));
	StoreVOperand(ins->o(0), val);
}
void LLVMGen::Emit_add(qir::InstBinop *ins)
{
	auto val = lb->CreateAdd(LoadVOperand(ins->i(0)), LoadVOperand(ins->i(1)));
	StoreVOperand(ins->o(0), val);
}
void LLVMGen::Emit_sub(qir::InstBinop *ins)
{
	unreachable("");
}
void LLVMGen::Emit_and(qir::InstBinop *ins)
{
	unreachable("");
}
void LLVMGen::Emit_or(qir::InstBinop *ins)
{
	unreachable("");
}
void LLVMGen::Emit_xor(qir::InstBinop *ins)
{
	unreachable("");
}
void LLVMGen::Emit_sra(qir::InstBinop *ins)
{
	unreachable("");
}
void LLVMGen::Emit_srl(qir::InstBinop *ins)
{
	unreachable("");
}
void LLVMGen::Emit_sll(qir::InstBinop *ins)
{
	unreachable("");
}

struct LLVMGenVisitor : qir::InstVisitor<LLVMGenVisitor, void> {
public:
	LLVMGenVisitor(LLVMGen *cg_) : cg(cg_) {}

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

llvm::Function *LLVMGen::Run()
{
	llvm::FunctionType *qcg_ftype =
	    llvm::FunctionType::get(llvm::Type::getVoidTy(*ctx),
				    {llvm::Type::getInt8PtrTy(*ctx), llvm::Type::getInt8PtrTy(*ctx)}, false);

	qcg_jitabi_nevercalled = llvm::Function::Create(qcg_ftype, llvm::Function::ExternalLinkage,
							"qcgstub_nevercalled", cmodule);
	qcgfn_null = llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(qcg_ftype));

	func = llvm::Function::Create(qcg_ftype, llvm::Function::ExternalLinkage, MakeAotSymbol(region_ip),
				      cmodule);

	func->setCallingConv(llvm::CallingConv::GHC);
	func->getArg(0)->addAttr(llvm::Attribute::NoAlias);
	func->getArg(1)->addAttr(llvm::Attribute::NoAlias);

	statev = func->getArg(0);
	membasev = func->getArg(1);
	statev->setName("state");
	membasev->setName("membase");

	auto *entry_lbb = llvm::BasicBlock::Create(*ctx, "entry", func);
	lb->SetInsertPoint(entry_lbb);
	CreateVGPRLocs();

	auto &blist = region->blist;

	for (auto &bb : blist) {
		auto id = bb.GetId();
		auto *lbb = llvm::BasicBlock::Create(*ctx, "bb." + std::to_string(bb.GetId()), func);
		if (id == 0) { // TODO: set first bb in qir
			lb->CreateBr(lbb);
		}
		id2bb.insert({id, lbb});
	}

	for (auto &bb : blist) {
		auto lbb = id2bb.find(bb.GetId())->second;
		lb->SetInsertPoint(lbb);
		qbb = &bb;

		auto &ilist = bb.ilist;
		for (auto iit = ilist.begin(); iit != ilist.end(); ++iit) {
			LLVMGenVisitor(this).visit(&*iit);
		}
	}
	// func->print(llvm::errs(), nullptr);
	cmodule->print(llvm::errs(), nullptr, true, true);
	assert(!verifyFunction(*func, &llvm::errs()));
	return func;
}

struct LLVMGenPass {
	static llvm::Function *run(qir::Region *region, u32 ip, llvm::LLVMContext *ctx,
				   llvm::Module *cmodule);
};

llvm::Function *LLVMGenPass::run(qir::Region *region, u32 ip, llvm::LLVMContext *ctx, llvm::Module *cmodule)
{
	LLVMGen gen(region, ip, ctx, cmodule);
	return gen.Run();
}

// TODO: move this somewhere
Region *CompilerGenRegionIR(MemArena *arena, CompilerJob &job);

} // namespace qir

struct LLVMAOTCompilerRuntime final : CompilerRuntime {
	LLVMAOTCompilerRuntime() {}

	void *AllocateCode(size_t sz, uint align) override
	{
		unreachable("");
	}

	bool AllowsRelocation() const override
	{
		unreachable("");
	}

	// TODO: seems like it's related to segment!
	uptr GetVMemBase() const override
	{
		return (uptr)mmu::base;
	}

	void *AnnounceRegion(u32 ip, std::span<u8> const &code) override
	{
		unreachable("");
	}
};

static void LLVMAOTCompilePage(CompilerRuntime *aotrt, std::vector<AOTSymbol> *aot_symbols,
			       llvm::LLVMContext *ctx, llvm::Module *cmodule, objprof::PageData const &page,
			       llvm::FunctionPassManager *fpm, llvm::FunctionAnalysisManager *fam)
{
	auto mg = BuildModuleGraph(page);
	auto regions = mg.ComputeRegions();

#if 0
	for (auto const &r : regions) {
		assert(r[0]->flags.region_entry);
		qir::CompilerJob::IpRangesSet ipranges;
		for (auto n : r) {
			ipranges.push_back({n->ip, n->ip_end});
		}

		qir::CompilerJob job(aotrt, mg.segment, std::move(ipranges));
		qir::CompilerDoJob(job);

		aot_symbols->push_back({r[0].ip, 0});
	}
#else
	for (auto const &e : mg.ip_map) {
		auto const &n = *e.second;
		qir::CompilerJob job(aotrt, mg.segment, {{n.ip, n.ip_end}});

		auto arena = MemArena(1_MB);
		auto *region = qir::CompilerGenRegionIR(&arena, job);

		auto func = qir::LLVMGenPass::run(region, n.ip, ctx, cmodule);
		fpm->run(*func, *fam);
		// func->print(llvm::errs(), nullptr);

		aot_symbols->push_back({n.ip, 0});
		break;
	}
#endif
}

static void AddAOTTabSection(llvm::LLVMContext &ctx, llvm::Module &cmodule,
			     std::vector<AOTSymbol> &aot_symbols)
{
	size_t aottab_size = sizeof(AOTTabHeader) + sizeof(aot_symbols[0]) * aot_symbols.size();
	auto type = llvm::ArrayType::get(llvm::Type::getInt8Ty(ctx), aottab_size);
	auto zeroinit = llvm::ConstantAggregateZero::get(type);
	auto aottab = new llvm::GlobalVariable(cmodule, type, true, llvm::GlobalVariable::ExternalLinkage,
					       zeroinit, AOT_SYM_AOTTAB);

	aottab->setAlignment(llvm::Align(alignof(AOTTabHeader)));
	aottab->setSection("aottab");
}

static void GenerateObjectFile(llvm::Module *cmodule, std::string const &filename)
{
	auto TargetTriple = llvm::sys::getDefaultTargetTriple();
	llvm::InitializeNativeTarget();
	llvm::InitializeNativeTargetAsmPrinter();
	std::string Error;
	auto Target = llvm::TargetRegistry::lookupTarget(TargetTriple, Error);
	if (!Target) {
		Panic(Error);
	}

	auto CPU = "generic";
	auto Features = "";

	llvm::TargetOptions opt;
	auto RM = llvm::Optional<llvm::Reloc::Model>();
	auto TargetMachine = Target->createTargetMachine(TargetTriple, CPU, Features, opt, RM);

	cmodule->setDataLayout(TargetMachine->createDataLayout());
	cmodule->setTargetTriple(TargetTriple);

	std::error_code EC;
	llvm::raw_fd_ostream dest(filename, EC, llvm::sys::fs::OF_None);

	if (EC) {
		llvm::errs() << "Could not open file: " << EC.message();
		Panic();
	}

	llvm::legacy::PassManager pass;
	auto FileType = llvm::CGFT_ObjectFile;

	if (TargetMachine->addPassesToEmitFile(pass, dest, nullptr, FileType)) {
		llvm::errs() << "TargetMachine can't emit a file of this type";
		Panic();
	}

	pass.run(*cmodule);
	dest.flush();
}

void LLVMAOTCompileELF()
{
	auto ctx = llvm::LLVMContext();
	auto cmodule = llvm::Module("test_module", ctx);

	llvm::LoopAnalysisManager lam;
	llvm::FunctionAnalysisManager fam;
	llvm::CGSCCAnalysisManager cgam;
	llvm::ModuleAnalysisManager mam;

	llvm::PassBuilder pb;
	pb.registerModuleAnalyses(mam);
	pb.registerCGSCCAnalyses(cgam);
	pb.registerFunctionAnalyses(fam);
	pb.registerLoopAnalyses(lam);
	pb.crossRegisterProxies(lam, fam, cgam, mam);

	llvm::FunctionPassManager fpm = pb.buildFunctionSimplificationPipeline(
	    llvm::OptimizationLevel::O3, llvm::ThinOrFullLTOPhase::None);
	llvm::ModulePassManager mpm = pb.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O3);

	auto aotrt = LLVMAOTCompilerRuntime{};
	std::vector<AOTSymbol> aot_symbols;
	aot_symbols.reserve(64_KB);

	for (auto const &page : objprof::GetProfile()) {
		LLVMAOTCompilePage(&aotrt, &aot_symbols, &ctx, &cmodule, page, &fpm, &fam);
		break;
	}
	assert(!verifyModule(cmodule, &llvm::errs()));
	// assert(0);
	AddAOTTabSection(ctx, cmodule, aot_symbols);

	auto obj_path = objprof::GetCachePath(AOT_O_EXTENSION);
	GenerateObjectFile(&cmodule, obj_path);
	ExecuteAOTLinker(aot_symbols);

	// assert(0);
}

} // namespace dbt

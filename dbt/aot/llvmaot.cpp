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
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetIntrinsicInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"

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
	llvm::Value *MakeStateEP(VType type, u32 offs);
	llvm::Value *MakeVOperandEP(VOperand op);

	static llvm::CmpInst::Predicate MakeCC(CondCode cc);
	llvm::Value *MakeRStub(RuntimeStubId id, llvm::FunctionType *ftype);

	llvm::BasicBlock *MapBB(Block *bb);

	llvm::ConstantInt *const32(u32 val)
	{
		return llvm::ConstantInt::get(*ctx, llvm::APInt(32, val));
	}

	llvm::ConstantInt *const64(u64 val)
	{
		return llvm::ConstantInt::get(*ctx, llvm::APInt(64, val));
	}

	void EmitBinop(llvm::Instruction::BinaryOps opc, qir::InstBinop *ins);
	void EmitTrace();

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

	llvm::FunctionType *qcg_ftype{}, *qcg_htype{};
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
		auto state_ep = MakeStateEP(info->type, info->state_offs);
		state_ep->setName(std::string("@") + info->name);
		vlocs.push_back(state_ep);
	}

	for (RegN i = n_glob; i < vinfo->NumAll(); ++i) {
		auto type = vinfo->GetLocalType(i);
		auto name = "%" + std::to_string(i);
		auto state_ep = lb->CreateAlloca(MakeType(type), const32(VTypeToSize(type)), name);
		vlocs.push_back(state_ep);
	}
}

llvm::Value *LLVMGen::MakeRStub(RuntimeStubId id, llvm::FunctionType *ftype)
{
	auto val = lb->CreateConstInBoundsGEP1_32(lb->getInt8Ty(), statev,
						  offsetof(CPUState, stub_tab) + RuntimeStubTab::offs(id));

	auto fp_type = llvm::PointerType::getUnqual(ftype);

	auto state_ep = lb->CreateBitCast(val, llvm::PointerType::getUnqual(fp_type));
	return lb->CreateAlignedLoad(fp_type, state_ep, llvm::Align(8),
				     "#stub"); // TODO: typesize // TODO: dump
}

llvm::Value *LLVMGen::LoadVOperand(qir::VOperand op)
{
	auto type = op.GetType();
	if (op.IsConst()) {
		return const32(op.GetConst());
	}
	llvm::Value *state_ep = MakeVOperandEP(op);
	return lb->CreateAlignedLoad(MakeType(type), state_ep, llvm::Align(VTypeToSize(type)));
}

void LLVMGen::StoreVOperand(qir::VOperand op, llvm::Value *val)
{
	auto type = op.GetType();
	llvm::Value *state_ep = MakeVOperandEP(op);
	lb->CreateAlignedStore(val, state_ep, llvm::Align(VTypeToSize(type)));
}

llvm::Value *LLVMGen::MakeVOperandEP(VOperand op)
{
	assert(!op.IsConst());
	if (op.IsVGPR()) {
		return vlocs[op.GetVGPR()];
	}
	if (op.IsGSlot()) {
		return MakeStateEP(op.GetType(), op.GetSlotOffs());
	}
	unreachable("");
}

llvm::Value *LLVMGen::MakeStateEP(VType type, u32 offs)
{
	auto ep = lb->CreateConstInBoundsGEP1_32(lb->getInt8Ty(), statev, offs);
	return lb->CreateBitCast(ep, MakePtrType(type));
}

llvm::Value *LLVMGen::MakeVMemLoc(VType type, llvm::Value *addr)
{
	addr = lb->CreateZExt(addr, lb->getIntPtrTy(cmodule->getDataLayout()));
	auto ep = lb->CreateGEP(lb->getInt8Ty(), membasev, addr);
	return lb->CreateBitCast(ep, MakePtrType(type));
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

void LLVMGen::EmitTrace()
{
	auto qcg_trace_type =
	    llvm::FunctionType::get(llvm::Type::getVoidTy(*ctx), {llvm::Type::getInt8PtrTy(*ctx)}, false);
	lb->CreateCall(qcg_trace_type, MakeRStub(RuntimeStubId::id_trace, qcg_trace_type), {statev});
}

void LLVMGen::Emit_hcall(qir::InstHcall *ins)
{
	lb->CreateCall(qcg_htype, MakeRStub(ins->stub, qcg_htype), {statev, LoadVOperand(ins->i(0))});
	if (--qbb->ilist.end() == ins) { // TODO: hcall-terminator
		lb->CreateRetVoid();
	}
}
void LLVMGen::Emit_br(qir::InstBr *ins)
{
	auto &qbb_s = **qbb->GetSuccs().begin();

	lb->CreateBr(MapBB(&qbb_s));
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

// llvm doesn't support tailcalls for experimental.patchpoint, actual call is not expanded until
// MCInstLowering. The same applies to gc.statepoint
// TODO: Seems like I can implelemnt custom MCInst to replace desired tailcalls like XRay MFPass does
//
// llvm corrupts ghccc Sp in MFs with stackmap, even if stackmap records no values
// I applied this in SelectionDAGBuilder for stackmaps with no liveins:
// + FuncInfo.MF->getFrameInfo().setHasStackMap(CI.arg_size() > 2);
// and the same for patchpoints
void LLVMGen::Emit_gbr(qir::InstGBr *ins)
{
	// Relocation is not necessary, also would overwrite patchpoint data, avoid
#if 1
	std::array<llvm::Value *, 7> args = {const64(ins->tpc.GetConst()),
					     const32(3 + sizeof(jitabi::ppoint::BranchSlot)),
					     llvm::ConstantPointerNull::get(llvm::Type::getInt8PtrTy(*ctx)),
					     // fake_callee,
					     const32(3), statev, membasev, func->getArg(2)};

	auto intr = lb->CreateIntrinsic(llvm::Intrinsic::experimental_patchpoint_void, {}, args);
	intr->addFnAttr(llvm::Attribute::NoReturn);
	intr->setCallingConv(llvm::CallingConv::GHC);
	intr->setTailCall(true);
	// intr->setTailCallKind(llvm::CallInst::TCK_MustTail);
	lb->CreateRetVoid();
#else
	auto stackmap = lb->CreateIntrinsic(
	    llvm::Intrinsic::experimental_stackmap, {},
	    {const64(ins->tpc.GetConst()), const32(3 + sizeof(jitabi::ppoint::BranchSlot))});
	stackmap->setCallingConv(llvm::CallingConv::GHC);
	stackmap->setTailCall(true);
#endif
	// auto call = lb->CreateCall(qcg_ftype, fake_callee, {statev, membasev, func->getArg(2)});
	// call->addFnAttr(llvm::Attribute::NoReturn);
	// call->setCallingConv(llvm::CallingConv::GHC);
	// call->setTailCall(true);
	// call->setTailCallKind(llvm::CallInst::TCK_MustTail);
	// lb->CreateRetVoid();
}
void LLVMGen::Emit_gbrind(qir::InstGBrind *ins)
{
	// TODO: fastpath, cache fntype
	auto retpair_type =
	    llvm::StructType::create({lb->getInt8PtrTy(), llvm::PointerType::getUnqual(qcg_ftype)});
	auto qcg_brind_fntype = llvm::FunctionType::get(
	    retpair_type, {llvm::Type::getInt8PtrTy(*ctx), llvm::Type::getInt32Ty(*ctx)}, false);

	auto retpair = lb->CreateCall(qcg_brind_fntype, MakeRStub(RuntimeStubId::id_brind, qcg_brind_fntype),
				      {statev, LoadVOperand(ins->i(0))});
	auto target = lb->CreateExtractValue(retpair, {1});

	auto call = lb->CreateCall(qcg_ftype, target, {statev, membasev, func->getArg(2)});
	call->addFnAttr(llvm::Attribute::NoReturn);
	call->setCallingConv(llvm::CallingConv::GHC);
	call->setTailCall(true);
	call->setTailCallKind(llvm::CallInst::TCK_MustTail);
	lb->CreateRetVoid();
}
void LLVMGen::Emit_vmload(qir::InstVMLoad *ins)
{
	// TODO: resolve types mess
	llvm::MaybeAlign align = true ? llvm::MaybeAlign{} : llvm::Align(VTypeToSize(ins->sz));
	auto mem_ep = MakeVMemLoc(ins->sz, LoadVOperand(ins->i(0)));
	llvm::Value *val = lb->CreateAlignedLoad(MakeType(ins->sz), mem_ep, align);
	auto type = ins->o(0).GetType();
	if (type != ins->sz) {
		if (ins->sgn == VSign::S) {
			val = lb->CreateSExt(val, MakeType(type));
		} else {
			val = lb->CreateZExt(val, MakeType(type));
		}
	}
	StoreVOperand(ins->o(0), val);
}
void LLVMGen::Emit_vmstore(qir::InstVMStore *ins)
{
	auto val = LoadVOperand(ins->i(1));
	auto type = ins->i(1).GetType();
	if (type != ins->sz) {
		val = lb->CreateTrunc(val, MakeType(type));
	}
	llvm::MaybeAlign align = true ? llvm::MaybeAlign{} : llvm::Align(VTypeToSize(ins->sz));
	auto mem_ep = MakeVMemLoc(ins->sz, LoadVOperand(ins->i(0)));
	lb->CreateAlignedStore(val, mem_ep, align);
}
void LLVMGen::Emit_setcc(qir::InstSetcc *ins)
{
	auto cmp = lb->CreateICmp(MakeCC(ins->cc), LoadVOperand(ins->i(0)), LoadVOperand(ins->i(1)));
	StoreVOperand(ins->o(0), lb->CreateZExt(cmp, lb->getInt32Ty()));
}
void LLVMGen::Emit_mov(qir::InstUnop *ins)
{
	auto val = LoadVOperand(ins->i(0));
	StoreVOperand(ins->o(0), val);
}
void LLVMGen::EmitBinop(llvm::Instruction::BinaryOps opc, qir::InstBinop *ins)
{
	auto res = lb->CreateBinOp(opc, LoadVOperand(ins->i(0)), LoadVOperand(ins->i(1)));
	StoreVOperand(ins->o(0), res);
}
void LLVMGen::Emit_add(qir::InstBinop *ins)
{
	EmitBinop(llvm::Instruction::BinaryOps::Add, ins);
}
void LLVMGen::Emit_sub(qir::InstBinop *ins)
{
	EmitBinop(llvm::Instruction::BinaryOps::Sub, ins);
}
void LLVMGen::Emit_and(qir::InstBinop *ins)
{
	EmitBinop(llvm::Instruction::BinaryOps::And, ins);
}
void LLVMGen::Emit_or(qir::InstBinop *ins)
{
	EmitBinop(llvm::Instruction::BinaryOps::Or, ins);
}
void LLVMGen::Emit_xor(qir::InstBinop *ins)
{
	EmitBinop(llvm::Instruction::BinaryOps::Xor, ins);
}
void LLVMGen::Emit_sra(qir::InstBinop *ins)
{
	EmitBinop(llvm::Instruction::BinaryOps::AShr, ins);
}
void LLVMGen::Emit_srl(qir::InstBinop *ins)
{
	EmitBinop(llvm::Instruction::BinaryOps::LShr, ins);
}
void LLVMGen::Emit_sll(qir::InstBinop *ins)
{
	EmitBinop(llvm::Instruction::BinaryOps::Shl, ins);
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
	qcg_ftype = llvm::FunctionType::get(
	    llvm::Type::getVoidTy(*ctx),
	    {llvm::Type::getInt8PtrTy(*ctx), llvm::Type::getInt8PtrTy(*ctx), llvm::Type::getInt8PtrTy(*ctx)},
	    false);
	qcg_htype =
	    llvm::FunctionType::get(llvm::Type::getVoidTy(*ctx),
				    {llvm::Type::getInt8PtrTy(*ctx), llvm::Type::getInt32Ty(*ctx)}, false);

	// qcgfn_null = llvm::ConstantPointerNull::get(llvm::PointerType::getUnqual(qcg_ftype));

	func = llvm::Function::Create(qcg_ftype, llvm::Function::ExternalLinkage, MakeAotSymbol(region_ip),
				      cmodule);
	func->setCallingConv(llvm::CallingConv::GHC);
	func->getArg(0)->addAttr(llvm::Attribute::NoAlias);
	func->getArg(1)->addAttr(llvm::Attribute::NoAlias);
	func->getArg(2)->addAttr(llvm::Attribute::NoAlias);
	func->setDSOLocal(true);

	statev = func->getArg(0);
	membasev = func->getArg(1);
	statev->setName("state");
	membasev->setName("membase");
	func->getArg(2)->setName("spunwind");

	auto *entry_lbb = llvm::BasicBlock::Create(*ctx, "entry", func);
	lb->SetInsertPoint(entry_lbb);
	CreateVGPRLocs();

	// EmitTrace();

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
	func->print(llvm::errs(), nullptr);
	// cmodule->print(llvm::errs(), nullptr, true, true);
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

#if 1
	for (auto const &r : regions) {
		assert(r[0]->flags.region_entry);
		qir::CompilerJob::IpRangesSet ipranges;
		for (auto n : r) {
			ipranges.push_back({n->ip, n->ip_end});
		}

		qir::CompilerJob job(aotrt, mg.segment, std::move(ipranges));

		auto arena = MemArena(1_MB);
		auto *region = qir::CompilerGenRegionIR(&arena, job);

		auto entry_ip = r[0]->ip;
		auto func = qir::LLVMGenPass::run(region, entry_ip, ctx, cmodule);
		// func->print(llvm::errs(), nullptr);
		fpm->run(*func, *fam);
		// cmodule->print(llvm::errs(), nullptr, true, true);
		// func->print(llvm::errs(), nullptr);
		aot_symbols->push_back({entry_ip, 0});
	}
#else
	for (auto const &e : mg.ip_map) {
		auto const &n = *e.second;
		qir::CompilerJob job(aotrt, mg.segment, {{n.ip, n.ip_end}});

		auto arena = MemArena(1_MB);
		auto *region = qir::CompilerGenRegionIR(&arena, job);

		auto func = qir::LLVMGenPass::run(region, n.ip, ctx, cmodule);
		// func->print(llvm::errs(), nullptr);
		fpm->run(*func, *fam);
		// cmodule->print(llvm::errs(), nullptr, true, true);
		func->print(llvm::errs(), nullptr);
		aot_symbols->push_back({n.ip, 0});
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
	aottab->setSection(".aottab");
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
	auto RM = std::optional<llvm::Reloc::Model>();
	auto TargetMachine = Target->createTargetMachine(TargetTriple, CPU, Features, opt, RM);

	cmodule->setDataLayout(TargetMachine->createDataLayout());
	cmodule->setTargetTriple(TargetTriple);
	cmodule->setPICLevel(llvm::PICLevel::SmallPIC);

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
	}
	assert(!verifyModule(cmodule, &llvm::errs()));
	mpm.run(cmodule, mam);
	AddAOTTabSection(ctx, cmodule, aot_symbols);

	auto obj_path = objprof::GetCachePath(AOT_O_EXTENSION);
	GenerateObjectFile(&cmodule, obj_path);
	ProcessLLVMStackmaps(aot_symbols);
	LinkAOTObject(aot_symbols);
}

} // namespace dbt

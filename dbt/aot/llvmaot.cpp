#include "dbt/aot/aot.h"
#include "dbt/guest/rv32_cpu.h"
#include "dbt/qmc/llvmgen/llvmgen.h"
#include "dbt/qmc/qcg/jitabi.h"
#include "dbt/tcache/objprof.h"

#include "llvm/IR/LegacyPassManager.h"
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

namespace dbt
{
LOG_STREAM(aot)

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

	void *AnnounceRegion(u32 ip, std::span<u8> const &code) override
	{
		unreachable("");
	}
};

static void LLVMAOTTranslatePage(CompilerRuntime *aotrt, std::vector<AOTSymbol> *aot_symbols,
				 llvm::Module *cmodule, objprof::PageData const &page,
				 llvm::FunctionPassManager *fpm, llvm::FunctionAnalysisManager *fam)
{
	auto mg = BuildModuleGraph(page);
	auto regions = mg.ComputeRegions();

	qir::LLVMGen llvm_gen(&cmodule->getContext(), cmodule, &mg.segment);

	for (auto const &r : regions) {
		llvm_gen.AddFunction(r[0]->ip);
	}

	for (auto const &r : regions) {
		assert(r[0]->flags.region_entry);
		qir::CompilerJob::IpRangesSet ipranges;
		for (auto n : r) {
			ipranges.push_back({n->ip, n->ip_end});
		}

		qir::CompilerJob job(aotrt, (uptr)mmu::base, mg.segment, std::move(ipranges));

		auto arena = MemArena(1_MB);
		auto *region = qir::CompilerGenRegionIR(&arena, job);

		auto entry_ip = r[0]->ip;
		auto func = llvm_gen.Run(region, entry_ip);
		fpm->run(*func, *fam);
		aot_symbols->push_back({entry_ip, 0});
	}
}

static void AddAOTTabSection(llvm::Module &cmodule, std::vector<AOTSymbol> &aot_symbols)
{
	size_t aottab_size = sizeof(AOTTabHeader) + sizeof(aot_symbols[0]) * aot_symbols.size();
	auto type = llvm::ArrayType::get(llvm::Type::getInt8Ty(cmodule.getContext()), aottab_size);
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
	llvm::InitializeNativeTargetAsmParser();
	std::string Error;
	auto Target = llvm::TargetRegistry::lookupTarget(TargetTriple, Error);
	if (!Target) {
		Panic(Error);
	}

	auto CPU = "generic"; // TODO(tuning): select native
	auto Features = "";

	llvm::TargetOptions opt;
	auto RM = llvm::Reloc::Model();
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

static thread_local llvm::LLVMContext g_llvm_ctx;

void LLVMAOTCompileELF()
{
	auto cmodule = llvm::Module("test_module", g_llvm_ctx);

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
		LLVMAOTTranslatePage(&aotrt, &aot_symbols, &cmodule, page, &fpm, &fam);
	}
	assert(!verifyModule(cmodule, &llvm::errs()));
	mpm.run(cmodule, mam);
	AddAOTTabSection(cmodule, aot_symbols);

	auto obj_path = objprof::GetCachePath(AOT_O_EXTENSION);
	GenerateObjectFile(&cmodule, obj_path);
	// ProcessLLVMStackmaps(aot_symbols);
	LinkAOTObject(aot_symbols);
}

} // namespace dbt

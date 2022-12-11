#pragma once

#include "dbt/common.h"
#include "dbt/qjit/qir.h"
#include "dbt/qjit/regalloc.h"
#include "dbt/tcache/tcache.h"
#include <array>

#include "dbt/qjit/qjit_stubs.h"

namespace dbt::qjit
{
LOG_STREAM(qjit);

struct QuickJIT;

struct Codegen {
	static constexpr auto STATE = asmjit::x86::Gp::kIdR13;
	static constexpr auto MEMBASE = asmjit::x86::Gp::kIdR12;
	static constexpr auto SP = asmjit::x86::Gp::kIdSp;
	static constexpr auto TMP1C = asmjit::x86::Gp::kIdCx;

	Codegen();
	void SetupCtx(QuickJIT *ctx_);
	void Prologue();
	void Epilogue();
	void EmitCode();
	void DumpCode();

	struct JitErrorHandler : asmjit::ErrorHandler {
		virtual void handleError(asmjit::Error err, const char *message,
					 asmjit::BaseEmitter *origin) override
		{
			Panic("jit codegen failed");
		}
	};

	void Bind(asmjit::Label l);
	void BranchCC(asmjit::Label taken, asmjit::x86::CondCode cc, asmjit::Operand lhs,
		      asmjit::Operand rhs);
	void SetCC(asmjit::x86::CondCode cc, asmjit::Operand rd, asmjit::Operand lhs, asmjit::Operand rhs);

	void Call(asmjit::Operand const *args, u8 nargs);
	void BranchTBDir(u32 ip);
	void BranchTBInd(asmjit::Operand target);

	void x86Cmp(asmjit::x86::CondCode *cc, asmjit::Operand lhs, asmjit::Operand rhs);

	void Spill(RegAlloc::VReg *v); // Internal
	void Fill(RegAlloc::VReg *v);  // Internal

	asmjit::JitRuntime jrt{};
	asmjit::CodeHolder jcode{};
	asmjit::x86::Assembler j{};
	JitErrorHandler jerr{};

	QuickJIT *ctx{};
};

struct QuickJIT {
	QuickJIT();
	~QuickJIT();
	QuickJIT(QuickJIT &) = delete;
	QuickJIT(QuickJIT &&) = delete;

	RegAlloc *ra{nullptr};
	Codegen *cg{nullptr};
	TBlock *tb{nullptr};

	RegAlloc::VReg *vreg_ip{nullptr};
};

}; // namespace dbt::qjit
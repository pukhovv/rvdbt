#pragma once

#include "dbt/qjit/qir.h"
#include "dbt/qjit/qjit.h"

#include <vector>

namespace dbt::qcg
{
LOG_STREAM(qcg);

struct QuickCG {
	static constexpr auto STATE = asmjit::x86::Gp::kIdR13;
	static constexpr auto MEMBASE = asmjit::x86::Gp::kIdR12;
	static constexpr auto SP = asmjit::x86::Gp::kIdSp;
	static constexpr auto TMP1C = asmjit::x86::Gp::kIdCx;

	void Generate(qir::Region *r);

	QuickCG();

#define OP(name, cls) void Emit_##name(qir::cls *ins);
	QIR_OPS_LIST(OP)
#undef OP

	template <asmjit::x86::Inst::Id Op>
	ALWAYS_INLINE void EmitInstBinopCommutative(qir::InstBinop *ins);
	template <asmjit::x86::Inst::Id Op>
	ALWAYS_INLINE void EmitInstBinopNonCommutative(qir::InstBinop *ins);

	void SetupCtx();
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

	asmjit::JitRuntime jrt{};
	asmjit::CodeHolder jcode{};
	asmjit::x86::Assembler j{};
	JitErrorHandler jerr{};

	TBlock *tb;
	qir::Block *bb;
	std::vector<asmjit::Label> labels; // TODO:
};

}; // namespace dbt::qcg
#pragma once

#include "dbt/qjit/qcg/arch_traits.h"
#include "dbt/qjit/qcg/jitabi.h"
#include "dbt/qjit/qcg/qcg.h"

#include <vector>

namespace dbt::qcg
{
struct QEmit {
	QEmit(qir::Region *region, bool jit_mode_);

	inline void SetBlock(qir::Block *bb_)
	{
		bb = bb_;
		j.bind(labels[bb->GetId()]);
	}

	TBlock::TCode EmitTCode();
	static void DumpTCode(TBlock::TCode const &tc);

	void Prologue(u32 ip);
	void StateSpill(qir::RegN p, qir::VType type, u16 offs);
	void StateFill(qir::RegN p, qir::VType type, u16 offs);
	void LocSpill(qir::RegN p, qir::VType type, u16 offs);
	void LocFill(qir::RegN p, qir::VType type, u16 offs);

#define OP(name, cls, flags) void Emit_##name(qir::cls *ins);
	QIR_OPS_LIST(OP)
#undef OP

	static constexpr auto R_STATE = asmjit::x86::gpq(ArchTraits::STATE);
	static constexpr auto R_MEMBASE = asmjit::x86::gpq(ArchTraits::MEMBASE);
	static constexpr auto R_SP = asmjit::x86::gpq(ArchTraits::SP);
	static constexpr auto R_TMP1 = asmjit::x86::gpq(ArchTraits::TMP1);

private:
	template <asmjit::x86::Inst::Id Op>
	ALWAYS_INLINE void EmitInstBinop(qir::InstBinop *ins);

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

	std::vector<asmjit::Label> labels;

	qir::Block *bb{};

	bool jit_mode{};
	RuntimeStubTab const &stub_tab{*RuntimeStubTab::GetGlobal()};
};

}; // namespace dbt::qcg
#pragma once

#include "dbt/qmc/qcg/arch_traits.h"
#include "dbt/qmc/qcg/jitabi.h"
#include "dbt/qmc/qcg/qcg.h"

#include <vector>

namespace dbt::qcg
{
struct QEmit {
	QEmit(qir::Region *region, CompilerRuntime *cruntime_, qir::CodeSegment *segment_, bool is_leaf_);

	inline void SetBlock(qir::Block *bb_)
	{
		bb = bb_;
		j.bind(labels[bb->GetId()]);
	}

	std::span<u8> EmitCode();
	static void DumpCode(std::span<u8> const &code);

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

private:
	void FrameSetup();
	void FrameDestroy();

	template <asmjit::x86::Inst::Id Op>
	ALWAYS_INLINE void EmitInstBinop(qir::InstBinop *ins);

	struct JitErrorHandler : asmjit::ErrorHandler {
		virtual void handleError(asmjit::Error err, const char *message,
					 asmjit::BaseEmitter *origin) override
		{
			Panic("qemit asmjit failed");
		}
	};

	inline asmjit::Operand make_operand(qir::VOperand opr);
	inline asmjit::x86::Mem make_slot(qir::VOperand opr);
	inline asmjit::Operand make_stubcall_target(RuntimeStubId stub);

	qir::Block *bb{};

	CompilerRuntime *cruntime{};
	qir::CodeSegment *segment{};
	bool jit_mode;

	RuntimeStubTab const &stub_tab{*RuntimeStubTab::GetGlobal()};

	bool is_leaf;
	u32 spillframe_sp_offs;

	asmjit::JitRuntime jrt{};
	asmjit::CodeHolder jcode{};
	asmjit::x86::Assembler j{};
	JitErrorHandler jerr{};

	std::vector<asmjit::Label> labels;
};

}; // namespace dbt::qcg
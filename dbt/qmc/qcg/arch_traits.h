#pragma once

#include "dbt/qmc/qcg/asmjit_deps.h"
#include "dbt/qmc/qir.h"

namespace dbt::qcg
{

struct RegMask {
	constexpr RegMask(u32 data_) : data(data_) {}

	constexpr inline bool Test(qir::RegN r) const
	{
		return data & (1u << r);
	}
	constexpr inline RegMask &Set(qir::RegN r)
	{
		data |= (1u << r);
		return *this;
	}
	constexpr inline RegMask &Clear(qir::RegN r)
	{
		data &= ~(1u << r);
		return *this;
	}
	constexpr inline u8 count() const
	{
		return std::popcount(data);
	}
	constexpr inline RegMask operator&(RegMask rh) const
	{
		return RegMask{data & rh.data};
	}
	constexpr inline RegMask operator|(RegMask rh) const
	{
		return RegMask{data | rh.data};
	}
	constexpr inline RegMask operator~() const
	{
		return RegMask{~data};
	}
	constexpr inline auto GetData() const
	{
		return data;
	}

private:
	u32 data;
};

enum class RACtImm : u8 {
	NO = 0 << 0,
	ANY = 1 << 0,
	// amd64
	S32 = 1 << 1,
	U32 = 1 << 2,
};
DEFINE_ENUM_CLASS_FLAGOPS(RACtImm)

struct RAOpCt {
	constexpr inline void SetAlias(u8 alias_)
	{
		has_alias = true;
		alias = alias_;
	}

	RegMask cr{0};
	RACtImm ci{};
	bool has_alias{};
	u8 alias{};
};

namespace ArchTraits
{
static constexpr u8 GPR_NUM = 16;

#define DEF_GPR(name, id) [[maybe_unused]] static constexpr auto name = asmjit::x86::Gp::kId##id;
// all gpr
DEF_GPR(RAX, Ax);
DEF_GPR(RCX, Cx);
DEF_GPR(RDX, Dx);
DEF_GPR(RBX, Bx);
DEF_GPR(RSP, Sp);
DEF_GPR(RBP, Bp);
DEF_GPR(RSI, Si);
DEF_GPR(RDI, Di);
DEF_GPR(R8, R8);
DEF_GPR(R9, R9);
DEF_GPR(R10, R10);
DEF_GPR(R11, R11);
DEF_GPR(R12, R12);
DEF_GPR(R13, R13);
DEF_GPR(R14, R14);
DEF_GPR(R15, R15);
#undef DEF_GPR

#define DEF_FIXED(name, reg) [[maybe_unused]] static constexpr auto name = reg;
// qmc fixed regs
DEF_FIXED(STATE, R13);	 // ghccc0
DEF_FIXED(MEMBASE, RBP); // ghccc1
DEF_FIXED(SP, RSP);
#undef DEF_FIXED

static constexpr RegMask GPR_FIXED = RegMask(0)
					 .Set(STATE)
#ifndef CONFIG_ZERO_MMU_BASE
					 .Set(MEMBASE)
#endif
					 .Set(SP);

static constexpr RegMask GPR_CALL_CLOBBER =
    RegMask(0).Set(RAX).Set(RDI).Set(RSI).Set(RDX).Set(RCX).Set(R8).Set(R9).Set(R10).Set(R11);

static constexpr RegMask GPR_ALL(((u32)1 << GPR_NUM) - 1);
static constexpr RegMask GPR_POOL = GPR_ALL & ~GPR_FIXED;
static constexpr RegMask GPR_CALL_SAVED = GPR_ALL & ~GPR_CALL_CLOBBER;

#define QCG_SPILL_FRAME_SIZE 480 // TODO: reuse temps
static constexpr u16 spillframe_size = QCG_SPILL_FRAME_SIZE;

bool match_gp_const(qir::VType type, i64 val, RACtImm ct);

void init();
} // namespace ArchTraits

} // namespace dbt::qcg

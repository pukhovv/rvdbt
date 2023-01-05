#pragma once

#include "dbt/qjit/qcg/jitabi.h"
#include "dbt/qjit/qir_builder.h"
#include "dbt/tcache/tcache.h"

#include "dbt/qjit/qcg/asmjit_deps.h"

#include <vector>

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

namespace ArchTraits
{
// TODO: as list
#define FIX_GPR_LIST(X)                                                                                      \
	X(STATE, kIdR13)                                                                                     \
	X(MEMBASE, kIdR12)                                                                                   \
	X(SP, kIdSp)                                                                                         \
	X(TMP1, kIdAx)

#define DEF_GPR(name, id) static constexpr auto name = asmjit::x86::Gp::id;
FIX_GPR_LIST(DEF_GPR)
#undef DEF_GPR

#define DEF_GPR(name, id) .Set(name)
static constexpr RegMask __GPR_FIXED = RegMask(0) FIX_GPR_LIST(DEF_GPR);
#undef DEF_GPR

#ifdef CONFIG_ZERO_MMU_BASE
static constexpr RegMask GPR_FIXED = RegMask(__GPR_FIXED).Clear(MEMBASE);
#else
static constexpr RegMask GPR_FIXED = RegMask(__GPR_FIXED);
#endif

static constexpr u8 GPR_NUM = 16;
static constexpr RegMask GPR_ALL = ((u32)1 << GPR_NUM) - 1;
static constexpr RegMask GPR_POOL = GPR_ALL & ~GPR_FIXED;
#define R(name) .Set(asmjit::x86::Gp::Id::kId##name)
static constexpr RegMask GPR_CALL_CLOBBER =
    RegMask(0) R(Ax) R(Di) R(Si) R(Dx) R(Cx) R(R8) R(R9) R(R10) R(R11);
#undef R
static constexpr RegMask GPR_CALL_SAVED = GPR_ALL & ~GPR_CALL_CLOBBER;
} // namespace ArchTraits

static constexpr u16 stub_frame_size = 248;

struct QRegAllocPass {
	static void run(qir::Region *region);
};

} // namespace dbt::qcg

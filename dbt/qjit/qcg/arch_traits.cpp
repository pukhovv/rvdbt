#include "dbt/qjit/qcg/arch_traits.h"
#include <algorithm>
#include <numeric>

namespace dbt::qcg
{

struct RACtDef {
	RACtDef() = default;
	constexpr RACtDef(RegMask cr_) : cr(cr_) {}
	constexpr RACtDef(RegMask cr_, RACtImm ci_) : cr(cr_), ci(ci_) {}
	RegMask cr{0};
	RACtImm ci{RACtImm::NO};
};
struct RACtDefOrAlias : RACtDef {
	constexpr RACtDefOrAlias(RACtDef o) : RACtDef(o.cr, o.ci) {}
	constexpr RACtDefOrAlias(u8 alias_) : has_alias(true), alias(alias_) {}
	bool has_alias{};
	u8 alias{};
};

template <u8 N_OUT, u8 N_IN>
struct InstCt {
	using ct_desc = std::array<RAOpCt, N_OUT + N_IN>;
	using ct_order = std::array<u8, N_OUT + N_IN>;

	static constexpr InstCt Make(std::array<RACtDef, N_OUT> &&odef_set,
				     std::array<RACtDefOrAlias, N_IN> &&idef_set);

	ct_desc ct;
	ct_order order;
};

template <u8 N_OUT, u8 N_IN>
constexpr InstCt<N_OUT, N_IN> InstCt<N_OUT, N_IN>::Make(std::array<RACtDef, N_OUT> &&odef_set,
							std::array<RACtDefOrAlias, N_IN> &&idef_set)
{
	using ct_type = std::array<RAOpCt, N_OUT + N_IN>;
	ct_type ct;

	for (size_t oidx = 0; oidx < N_OUT; ++oidx) {
		auto odef = odef_set[oidx];
		ct[oidx] = {.cr = odef.cr, .ci = odef.ci};
	}

	for (size_t iidx = N_OUT; iidx < N_OUT + N_IN; ++iidx) {
		auto idef = idef_set[iidx - N_OUT];
		if (idef.has_alias) {
			ct[iidx].SetAlias(idef.alias);
			ct[idef.alias].SetAlias(iidx);
		} else {
			ct[iidx] = {.cr = idef.cr, .ci = idef.ci};
		}
	}

	std::array<u8, N_OUT + N_IN> order;

	auto order_ct = [&](auto const &b, auto const &e) {
		auto order_cmp = [&](u8 i0, u8 i1) { return ct[i0].cr.count() < ct[i1].cr.count(); };
		std::iota(b, e, 0);
		std::sort(b, e, order_cmp);
	};
	order_ct(order.begin(), order.begin() + N_OUT);
	order_ct(order.begin() + N_OUT, order.end());

	return {ct, order};
}

namespace RACtGPR
{
constexpr auto R = ArchTraits::GPR_ALL;
constexpr auto R8 = ArchTraits::GPR_ALL;
constexpr auto CX = RegMask(0).Set(ArchTraits::RCX);
constexpr auto SI = RegMask(0).Set(ArchTraits::RSI);
}; // namespace RACtGPR

#define GPR(X) RACtGPR::X
#define IMM(X) RACtImm::X
#define DEF(...) RACtDef(__VA_ARGS__)
#define ALIAS(X) RACtDefOrAlias(X)

// Add N$_ prefix for multiple-outs instructions
#define CT(name) static constinit auto CT_INFO_##name
CT(r_rs32) = InstCt<0, 2>::Make({}, {DEF(GPR(R)), DEF(GPR(R), IMM(S32))});
CT(si) = InstCt<0, 1>::Make({}, {DEF(GPR(SI))});
CT(r_ru32) = InstCt<1, 1>::Make({DEF(GPR(R))}, {DEF(GPR(R), IMM(U32))});
CT(r_ri) = InstCt<1, 1>::Make({DEF(GPR(R))}, {DEF(GPR(R), IMM(ANY))});
CT(ri_r) = InstCt<0, 2>::Make({}, {DEF(GPR(R), IMM(ANY)), DEF(GPR(R))});
CT(r8_r_rs32) = InstCt<1, 2>::Make({DEF(GPR(R8))}, {DEF(GPR(R)), DEF(GPR(R), IMM(S32))});
CT(r_0_rs32) = InstCt<1, 2>::Make({DEF(GPR(R))}, {ALIAS(0), DEF(GPR(R), IMM(S32))});
CT(r_0_ru32) = InstCt<1, 2>::Make({DEF(GPR(R))}, {ALIAS(0), DEF(GPR(R), IMM(U32))});
CT(r_0_cxi) = InstCt<1, 2>::Make({DEF(GPR(R))}, {ALIAS(0), DEF(GPR(CX), IMM(ANY))});
#undef CT

#undef GPR
#undef IMM
#undef DEF
#undef ALIAS

// TODO: verify, check cpuinfo
#define ARCH_OP_CT_LIST(CT)                                                                                  \
	CT(brcc, r_rs32)                                                                                     \
	CT(gbrind, si)                                                                                       \
	CT(vmload, r_ru32)                                                                                   \
	CT(vmstore, ri_r)                                                                                    \
	CT(setcc, r8_r_rs32)                                                                                 \
	CT(mov, r_ri)                                                                                        \
	CT(add, r_0_rs32)                                                                                    \
	CT(sub, r_0_rs32)                                                                                    \
	CT(and, r_0_ru32)                                                                                    \
	CT(or, r_0_rs32)                                                                                     \
	CT(xor, r_0_rs32)                                                                                    \
	CT(sra, r_0_cxi)                                                                                     \
	CT(srl, r_0_cxi)                                                                                     \
	CT(sll, r_0_cxi)

void ArchTraits::init()
{
	[[maybe_unused]] static auto x = []() {
#define CT(name, ctname)                                                                                     \
	{                                                                                                    \
		auto &info = qir::op_info[to_underlying(qir::Op::_##name)];                                  \
		info.ra_ct = CT_INFO_##ctname.ct.data();                                                     \
		info.ra_order = CT_INFO_##ctname.order.data();                                               \
	}
		ARCH_OP_CT_LIST(CT)
#undef CT
		return true;
	}();
}

bool match_gp_const(qir::VType type, i64 val, RACtImm ct)
{
	if (to_underlying(ct & RACtImm::ANY)) {
		return true;
	}
	if (to_underlying(ct & RACtImm::U32) && static_cast<u32>(val) == val) {
		return true;
	}
	if (to_underlying(ct & RACtImm::S32) && static_cast<i32>(val) == val) {
		return true;
	}
	return false;
}

} // namespace dbt::qcg

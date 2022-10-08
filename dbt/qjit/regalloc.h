#pragma once

#include "dbt/common.h"
#include "dbt/qjit/asmjit_deps.h"
#include <array>

namespace dbt::qjit
{

struct QuickJIT;

struct RegAlloc {
	static constexpr u8 N_PREGS = 16; // amd64
	using PReg = u8;
	struct Mask {
		constexpr Mask(u32 data_) : data(data_) {}

		constexpr inline bool Test(PReg r) const
		{
			return data & (1u << r);
		}
		constexpr inline void Set(PReg r)
		{
			data |= (1u << r);
		}
		constexpr inline void Clear(PReg r)
		{
			data &= ~(1u << r);
		}
		constexpr inline Mask operator&(Mask rh) const
		{
			return Mask{data & rh.data};
		}
		constexpr inline Mask operator|(Mask rh) const
		{
			return Mask{data | rh.data};
		}
		constexpr inline Mask operator~() const
		{
			return Mask{~data};
		}

	private:
		u32 data;
	};

	struct VReg {
		VReg() {}
		VReg(VReg &) = delete;
		VReg(VReg &&) = delete;

		enum class Type {
			I32,
			I64,
		};

		enum class Scope {
			LOC,
			GLOBAL,
			FIXED, // fixed regs are readonly
		};

		enum class Loc {
			DEAD,
			MEM,
			REG,
		};

		inline asmjit::x86::Gp GetPReg()
		{
			assert(loc == RegAlloc::VReg::Loc::REG);
			switch (type) {
			case RegAlloc::VReg::Type::I32:
				return asmjit::x86::gpd(p);
			case RegAlloc::VReg::Type::I64:
				return asmjit::x86::gpq(p);
			default:
				Panic();
			}
		}

		inline asmjit::x86::Mem GetSpill()
		{
			assert(loc == RegAlloc::VReg::Loc::MEM);
			return asmjit::x86::ptr(spill_base->GetPReg(), spill_offs,
						RegAlloc::TypeToSize(type));
		}

		// TODO: hide these
		char const *name{nullptr};
		Type type{};
		Loc loc{Loc::DEAD};
		Scope scope{};
		VReg *spill_base{nullptr};
		u16 spill_offs{};
		PReg p{};
		bool spill_synced{false};
		bool has_statemap{false};
	};

	static inline u8 TypeToSize(VReg::Type t)
	{
		switch (t) {
		case VReg::Type::I32:
			return 4;
		case VReg::Type::I64:
			return 8;
		default:
			Panic();
		}
	}

	struct AllocScope {
		AllocScope(RegAlloc *ra_) : ra(ra_)
		{
			prev = ra->ascope;
			ra->ascope = this;
			if (likely(prev)) {
				fixed = prev->fixed;
				num_vregs = prev->num_vregs;
				frame_cur = prev->frame_cur;
			}
		}
		~AllocScope()
		{
			assert(ra->ascope == this);
			ra->ascope = prev;
		}
		RegAlloc *ra{};
		AllocScope *prev{};

		Mask fixed{0};
		u16 num_vregs{0};
		u16 frame_cur{0};
	};

	void SetupCtx(QuickJIT *ctx_);

	RegAlloc() : ascope_global(this) {}

	PReg AllocPReg(Mask desire, Mask avoid);
	void Spill(PReg p);
	void Spill(VReg *v);
	void SyncSpill(VReg *v);
	template <bool kill>
	void Release(VReg *v);
	void AllocFrameSlot(VReg *v);
	void Fill(VReg *v, Mask desire, Mask avoid);

	VReg *AllocVReg();
	VReg *AllocVRegGlob(char const *name);
	VReg *AllocVRegFixed(char const *name, VReg::Type type, PReg p);
	VReg *AllocVRegMem(char const *name, VReg::Type type, VReg *base, u16 offs);

	void Prologue();
	void BlockBoundary();

	template <typename DstA, typename SrcA>
	void AllocOp(DstA &&dst, SrcA &&src, bool unsafe = false)
	{
		AllocOp(dst.data(), dst.size(), src.data(), src.size(), unsafe);
	}
	void AllocOp(VReg **dstl, u8 dst_n, VReg **srcl, u8 src_n, bool unsafe = false);
	void CallOp(bool use_globals = true);

	bool IsGlobalScope()
	{
		return ascope == &ascope_global;
	}

	AllocScope *ascope{};
	AllocScope ascope_global;

	std::array<VReg, 64> vregs{};

	VReg *state_base{nullptr};
	VReg *frame_base{nullptr};
	VReg *mem_base{nullptr};
	VReg *state_map{nullptr};

	std::array<VReg *, N_PREGS> p2v{nullptr};
	static constexpr u16 frame_size{32 * sizeof(u64)};

	QuickJIT *ctx{};
};

#define R(name) (1u << asmjit::x86::Gp::Id::kId##name)
static constexpr RegAlloc::Mask PREGS_ALL = ((u32)1 << 16) - 1;
static constexpr RegAlloc::Mask PREGS_CALL_CLOBBER{R(Ax) | R(Di) | R(Si) | R(Dx) | R(Cx) | R(R8) | R(R9) |
						   R(R10) | R(R11)};
static constexpr RegAlloc::Mask PREGS_CALL_SAVED = PREGS_ALL & ~PREGS_CALL_CLOBBER;
#undef R

} // namespace dbt::qjit

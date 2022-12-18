#pragma once

#include "dbt/qjit/qir.h"
#include "dbt/qjit/qjit.h"

#include <vector>

namespace dbt::qcg
{
LOG_STREAM(qcg);

struct RegMask {
	constexpr RegMask(u32 data_) : data(data_) {}

	constexpr inline bool Test(qir::PReg r) const
	{
		return data & (1u << r);
	}
	constexpr inline void Set(qir::PReg r)
	{
		data |= (1u << r);
	}
	constexpr inline void Clear(qir::PReg r)
	{
		data &= ~(1u << r);
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

private:
	u32 data;
};

struct QEmit {
	QEmit(qir::Region *region);

	inline void SetBlock(qir::Block *bb_)
	{
		bb = bb_;
	}

	TBlock *EmitTBlock();
	static void DumpTBlock(TBlock *tb);

	void Prologue();
	void StateSpill(qir::PReg p, qir::VType type, u16 offs);
	void StateFill(qir::PReg p, qir::VType type, u16 offs);
	void LocSpill(qir::PReg p, qir::VType type, u16 offs);
	void LocFill(qir::PReg p, qir::VType type, u16 offs);

#define OP(name, cls) void Emit_##name(qir::cls *ins);
	QIR_OPS_LIST(OP)
#undef OP

private:
#define DEF_FIX_REG(name, id)                                                                                \
	static constexpr auto name = asmjit::x86::Gp::id;                                                    \
	static constexpr auto R##name = asmjit::x86::gpq(name);

	DEF_FIX_REG(STATE, kIdR13);
	DEF_FIX_REG(MEMBASE, kIdR12);
	DEF_FIX_REG(SP, kIdSp);
	DEF_FIX_REG(TMP1C, kIdCx);
#undef DEF_FIX_REG

public:
#define R(name) (1u << asmjit::x86::Gp::Id::kId##name)
	static constexpr u8 GPR_NUM = 16;
	static constexpr RegMask GPR_ALL = ((u32)1 << GPR_NUM) - 1;
	static constexpr RegMask GPR_POOL = GPR_ALL & ~(STATE | MEMBASE | SP | TMP1C);
	static constexpr RegMask GPR_CALL_CLOBBER{R(Ax) | R(Di) | R(Si) | R(Dx) | R(Cx) | R(R8) | R(R9) |
						  R(R10) | R(R11)};
	static constexpr RegMask GPR_CALL_SAVED = GPR_ALL & ~GPR_CALL_CLOBBER;
#undef R

private:
	template <asmjit::x86::Inst::Id Op>
	ALWAYS_INLINE void EmitInstBinopCommutative(qir::InstBinop *ins);
	template <asmjit::x86::Inst::Id Op>
	ALWAYS_INLINE void EmitInstBinopNonCommutative(qir::InstBinop *ins);

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

	std::vector<asmjit::Label> labels; // TODO:

	qir::Block *bb{};
};

struct QRegAlloc {
	static constexpr auto N_PREGS = QEmit::GPR_NUM;
	static constexpr auto PREGS_POOL = QEmit::GPR_POOL;
	static constexpr auto MAX_VREGS = 128;

	struct RTrack {
		RTrack() {}
		RTrack(RTrack &) = delete;
		RTrack(RTrack &&) = delete;

		static constexpr auto NO_SPILL = static_cast<u16>(-1);

		qir::VType type{};
		bool is_global{};
		u16 spill_offs{NO_SPILL};

	private:
		friend QRegAlloc;

		enum class Location : u8 {
			DEAD,
			MEM,
			REG,
		};

		qir::PReg p{};
		Location loc{Location::DEAD};
		bool spill_synced{false};
	};

	QRegAlloc() {}

	qir::PReg AllocPReg(RegMask desire, RegMask avoid);
	void EmitSpill(RTrack *v);
	void EmitFill(RTrack *v);
	void Spill(qir::PReg p);
	void Spill(RTrack *v);
	void SyncSpill(RTrack *v);
	template <bool kill>
	void Release(RTrack *v);
	void AllocFrameSlot(RTrack *v);
	void Fill(RTrack *v, RegMask desire, RegMask avoid);

	RTrack *AddTrack();
	RTrack *AddTrackGlobal();
	RTrack *AddTrackGlobal(qir::VType type, u16 state_offs);
	RTrack *AddTrackLocal(qir::VType type);

	void Prologue();
	void BlockBoundary();

	template <typename DstA, typename SrcA>
	void AllocOp(DstA &&dst, SrcA &&src, bool unsafe = false)
	{
		AllocOp(dst.data(), dst.size(), src.data(), src.size(), unsafe);
	}
	void AllocOp(RTrack **dstl, u8 dst_n, RTrack **srcl, u8 src_n, bool unsafe = false);
	void CallOp(bool use_globals = true);

	std::array<RTrack, MAX_VREGS> vregs{};
	std::array<RTrack *, N_PREGS> p2v{nullptr};
	u16 n_vregs{0};

	RegMask fixed{0};
	static constexpr u16 frame_size{31 * sizeof(u64)};
	u16 frame_cur{0};

	QEmit *qe{};
};

struct QCodegen {
	static TBlock *Generate(qir::Region *r);

private:
	QCodegen(qir::Region *region_, QEmit *qe_) : region(region_), qe(qe_) {}

	void Translate();

	qir::Region *region;
	QEmit *qe;
};

}; // namespace dbt::qcg
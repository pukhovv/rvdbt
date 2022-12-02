#pragma once

#include "dbt/arena.h"
#include "dbt/common.h"
#include "dbt/logger.h"
#include "dbt/qjit/ilist.h"
#include "dbt/qjit/qir_ops.h"
#include "dbt/qjit/regalloc.h"

#include <type_traits>
#include <variant>

namespace dbt::qir
{
LOG_STREAM(qir);

template <typename D, typename B>
requires std::is_base_of_v<B, D> ALWAYS_INLINE D *as(B *b)
{
	if (!D::classof(b)) {
		return nullptr;
	}
	return static_cast<D *>(b);
}

template <typename D, typename B>
ALWAYS_INLINE D *cast(B *b)
{
	auto res = as<D>(b);
	assert(res);
	return res;
}

enum class Op : u8 {
#define OP(name, base) _##name,
#define GROUP(cls, beg, end) cls##_begin = _##beg, cls##_end = _##end,
	QIR_OPS_LIST(OP) QIR_GROUPS_LIST(GROUP)
#undef OP
#undef GROUP
	    Count,
};

enum class VType : u8 {
	UNDEF,
	I8,
	I16,
	I32,
	Count,
};

struct VOperandBase {
	bool IsConst() const
	{
		return is_const;
	}

	VType GetType() const
	{
		return type;
	}

	void SetType(VType type_)
	{
		type = type_;
	}

protected:
	VOperandBase(bool is_const_, VType type_) : is_const(is_const_), type(type_) {}

private:
	bool is_const;
	VType type;
};

union VOperand;

struct VConst : public VOperandBase {
	VConst(VType type_, u32 value_) : VOperandBase(true, type_), value(value_) {}

	static bool classof(VOperandBase *opr)
	{
		return opr->IsConst();
	}

	u32 GetValue() const
	{
		return value;
	}

private:
	u32 value;
};

struct VReg : VOperandBase {
	VReg(VType type_, int idx_) : VOperandBase(false, type_), idx(idx_) {}
	VReg() : VOperandBase(false, VType::UNDEF), idx{-1} {}

	static bool classof(VOperandBase *opr)
	{
		return !opr->IsConst();
	}

	u32 GetIdx() const
	{
		return idx;
	}

	VReg WithType(VType type_)
	{
		return VReg(type_, GetIdx());
	}

private:
	int idx;
};

union VOperand {
	VOperand(VConst cnst_) : cnst(cnst_) {}
	VOperand(VReg reg_) : reg(reg_) {}

	auto IsConst() const
	{
		return base.IsConst();
	}

	auto GetType() const
	{
		return base.GetType();
	}

	void SetType(VType type)
	{
		base.SetType(type);
	}

	VOperandBase &bcls()
	{
		return base;
	}

private:
	// illegal
	static_assert(static_cast<VOperandBase *>((VConst *)0) == 0);
	static_assert(static_cast<VOperandBase *>((VReg *)0) == 0);
	static_assert(std::is_trivially_copyable_v<VConst>);
	static_assert(std::is_trivially_copyable_v<VReg>);
	VOperandBase base;
	VConst cnst;
	VReg reg;
} __attribute__((may_alias));

struct Inst : IListNode<Inst> {
	inline Op GetOpcode() const
	{
		return opcode;
	}

	inline u32 GetId() const
	{
		return id;
	}

	inline void SetId(u32 id_)
	{
		id = id_;
	}

protected:
	inline Inst(Op opcode_) : opcode(opcode_) {}

private:
	Inst(Inst const &) = delete;
	Inst(Inst &&) = delete;
	Inst &operator=(Inst const &) = delete;
	Inst &operator=(Inst &&) = delete;

	friend struct Region;

	Op opcode;
	u32 id{(u32)-1};
};

template <size_t N_OUT, size_t N_IN>
struct InstWithOperands : Inst {
protected:
	InstWithOperands(Op opcode_, std::array<VReg, N_OUT> &&o_, std::array<VOperand, N_IN> &&i_)
	    : Inst(opcode_), o{o_}, i{i_}
	{
	}

public:
	std::array<VReg, N_OUT> o{};
	std::array<VOperand, N_IN> i{};
};

/* Common classes */

struct InstUnop : InstWithOperands<1, 1> {
	InstUnop(Op opcode_, VReg d, VOperand s) : InstWithOperands(opcode_, {d}, {s})
	{
		assert(HasOpcode(opcode_));
	}

	static bool classof(Inst *op)
	{
		return HasOpcode(op->GetOpcode());
	}

	static bool HasOpcode(Op opcode)
	{
		return opcode >= Op::InstUnop_begin && opcode <= Op::InstUnop_end;
	}
};

struct InstBinop : InstWithOperands<1, 2> {
	InstBinop(Op opcode_, VReg d, VOperand sl, VOperand sr) : InstWithOperands(opcode_, {d}, {sl, sr})
	{
		assert(HasOpcode(opcode_));
	}

	static bool classof(Inst *op)
	{
		return HasOpcode(op->GetOpcode());
	}

	static bool HasOpcode(Op opcode)
	{
		return opcode >= Op::InstBinop_begin && opcode <= Op::InstBinop_end;
	}
};

/* Custom classes */

struct Block;

struct InstBr : Inst {
	InstBr(Block *target_) : Inst(Op::_br), target(target_) {}

	Block *target;
};

enum class CondCode : u8 {
	EQ,
	NE,
	LT,
	GE,
	LTU,
	GEU,
	Count,
};

struct InstBrcc : InstWithOperands<0, 2> {
	InstBrcc(CondCode cc_, Block *bb_t_, Block *bb_f_, VOperand s1, VOperand s2)
	    : InstWithOperands(Op::_brcc, {}, {s1, s2}), cc(cc_), bb_t(bb_t_), bb_f(bb_f_)
	{
	}

	CondCode cc;
	Block *bb_t, *bb_f;
};

struct InstGBr : Inst {
	InstGBr(VConst tpc_) : Inst(Op::_gbr), tpc(tpc_) {}

	VConst tpc;
};

struct InstGBrind : InstWithOperands<0, 1> {
	InstGBrind(VReg tpc_) : InstWithOperands(Op::_gbrind, {}, {tpc_}) {}
};

struct InstHelper : Inst {
	InstHelper() : Inst(Op::_helper) {}
};

// TODO: format
struct InstVMLoad : InstWithOperands<1, 1> {
	InstVMLoad(VReg d, VOperand ptr) : InstWithOperands(Op::_vmload, {d}, {ptr}) {}
};

// TODO: format
struct InstVMStore : InstWithOperands<0, 2> {
	InstVMStore(VOperand ptr, VOperand val) : InstWithOperands(Op::_vmstore, {}, {ptr, val}) {}
};

struct Region;

struct Block : IListNode<Block> {
	Block(Region *rn_) : rn(rn_) {}

	IList<Inst> ilist;

	inline Region *GetRegion() const
	{
		return rn;
	}

	inline u32 GetId() const
	{
		return id;
	}

	inline void SetId(u32 id_)
	{
		id = id_;
	}

private:
	Region *rn;
	u32 id{(u32)-1};
};

struct Region {
	Region(MemArena *arena_) : arena(arena_) {}

	IList<Block> blist;

	Block *CreateBlock()
	{
		auto res = CreateInArena<Block>(this);
		res->SetId(bb_id_counter++);
		blist.insert(blist.end(), *res);
		return res;
	}

	template <typename T, typename... Args>
	requires std::is_base_of_v<Inst, T> T *Create(Args &&...args)
	{
		auto res = CreateInArena<T>(std::forward<Args>(args)...);
		res->SetId(inst_id_counter++);
		return res;
	}

private:
	template <typename T, typename... Args>
	T *CreateInArena(Args &&...args)
	{
		auto *mem = arena->Allocate<T>();
		assert(mem);
		return new (mem) T(std::forward<Args>(args)...);
	}

	MemArena *arena;
	u32 inst_id_counter{1};
	u32 bb_id_counter{1};
};

struct Builder {
	explicit Builder(Block *bb_) : bb(bb_), it(bb->ilist.end()) {}
	Builder(Block *bb_, IListIterator<Inst> it_) : bb(bb_), it(it_) {}

	Block *GetBlock() const
	{
		return bb;
	}

	Block *CreateBlock()
	{
		return bb->GetRegion()->CreateBlock();
	}

	template <typename T, typename... Args>
	T *Create(Args &&...args)
	{
		auto *ins = bb->GetRegion()->Create<T>(std::forward<Args>(args)...);
		bb->ilist.insert(it, *ins);
		return ins;
	}

#define OP(name, cls)                                                                                        \
	template <typename... Args>                                                                          \
	cls *Create_##name(Args &&...args)                                                                   \
	{                                                                                                    \
		return Create<cls>(Op::_##name, std::forward<Args>(args)...);                                \
	}
	QIR_SUBOPS_LIST(OP)
#undef OP

#define OP(name, cls)                                                                                        \
	template <typename... Args>                                                                          \
	cls *Create_##name(Args &&...args)                                                                   \
	{                                                                                                    \
		return Create<cls>(std::forward<Args>(args)...);                                             \
	}
	QIR_CLSOPS_LIST(OP)
#undef OP

#define GROUP(cls, beg, end)                                                                                 \
	template <typename... Args>                                                                          \
	cls *Create##cls(Args &&...args)                                                                     \
	{                                                                                                    \
		return Create<cls>(std::forward<Args>(args)...);                                             \
	}
	QIR_GROUPS_LIST(GROUP)
#undef GROUP

private:
	Block *bb;
	IListIterator<Inst> it;
};

template <typename Derived, typename RT>
struct InstVisitor {
#define VIS_CLASS(cls) return static_cast<Derived *>(this)->visit##cls(static_cast<cls *>(ins))

#define OP(name, cls)                                                                                        \
	RT visit_##name(cls *ins)                                                                            \
	{                                                                                                    \
		VIS_CLASS(cls);                                                                              \
	}
	QIR_OPS_LIST(OP)
#undef OP

#define OP(name, cls)                                                                                        \
	RT visit##cls(cls *ins)                                                                              \
	{                                                                                                    \
		VIS_CLASS(Inst);                                                                             \
	}
	QIR_CLSOPS_LIST(OP)
#undef OP

#define GROUP(cls, beg, end)                                                                                 \
	RT visit##cls(cls *ins)                                                                              \
	{                                                                                                    \
		VIS_CLASS(Inst);                                                                             \
	}
	QIR_GROUPS_LIST(GROUP)
#undef GROUP

	void visitInst(Inst *ins) {}

	void visit(Inst *ins)
	{
		switch (ins->GetOpcode()) {
#define OP(name, cls)                                                                                        \
	case Op::_##name:                                                                                    \
		return static_cast<Derived *>(this)->visit_##name(static_cast<cls *>(ins));
			QIR_OPS_LIST(OP)
#undef OP
		default:
			unreachable("");
		};
	}
};

} // namespace dbt::qir

#pragma once

#include "dbt/arena.h"
#include "dbt/common.h"
#include "dbt/logger.h"
#include "dbt/qjit/ilist.h"
#include "dbt/qjit/qir_ops.h"
#include "dbt/qjit/regalloc.h"

#include <variant>

namespace dbt::qir
{
LOG_STREAM(qir);

template <typename D, typename B>
requires std::is_base_of_v<B, D> ALWAYS_INLINE D *cast(B *b)
{
	if (!D::classof(b)) {
		return nullptr;
	}
	return static_cast<D *>(b);
}

template <typename D, typename B>
requires std::is_base_of_v<B, D> ALWAYS_INLINE D *as(B *b)
{
	auto res = cast<D>(b);
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

struct VOperand {
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
	VOperand(bool is_const_, VType type_) : is_const(is_const_), type(type_) {}

private:
	bool is_const;
	VType type;
};

struct VConst : VOperand {
	VConst(VType type_, u32 value_) : VOperand(true, type_), value(value_) {}

	static bool classof(VOperand *opr)
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
struct VReg : VOperand {
	VReg(VType type_, int idx_) : VOperand(false, type_), idx(idx_) {}
	VReg() : VOperand(false, VType::UNDEF), idx{-1} {}

	static bool classof(VOperand *opr)
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

union VOperandUn {
	VOperandUn(VConst cnst_) : cnst(cnst_) {}
	VOperandUn(VReg reg_) : reg(reg_) {}

	operator VOperand &()
	{
		return *reinterpret_cast<VOperand *>(this);
	}

private:
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
	InstWithOperands(Op opcode_, std::array<VReg, N_OUT> &&o_, std::array<VOperandUn, N_IN> &&i_)
	    : Inst(opcode_), o{o_}, i{i_}
	{
	}

public:
	std::array<VReg, N_OUT> o{};
	std::array<VOperandUn, N_IN> i{};
};

/* Common classes */

struct InstUnop : InstWithOperands<1, 1> {
	InstUnop(Op opcode_, VReg d, VOperandUn s) : InstWithOperands(opcode_, {d}, {s})
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
	InstBinop(Op opcode_, VReg d, VOperandUn sl, VOperandUn sr) : InstWithOperands(opcode_, {d}, {sl, sr})
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

struct InstLabel : Inst {
	InstLabel() : Inst(Op::_label) {}
};

struct Label {
	Label(InstLabel *ins_) : ins(ins_) {}

	InstLabel *GetInst()
	{
		return ins;
	}

private:
	InstLabel *ins;
};

struct InstBr : Inst {
	InstBr(Label target_) : Inst(Op::_br), target(target_) {}

	Label target;
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
	InstBrcc(CondCode cc_, Label target_, VOperandUn s1, VOperandUn s2)
	    : InstWithOperands(Op::_brcc, {}, {s1, s2}), cc(cc_), target(target_)
	{
	}

	CondCode cc;
	Label target;
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
	InstVMLoad(VReg d, VOperandUn ptr) : InstWithOperands(Op::_vmload, {d}, {ptr}) {}
};

// TODO: format
struct InstVMStore : InstWithOperands<0, 2> {
	InstVMStore(VOperandUn ptr, VOperandUn val) : InstWithOperands(Op::_vmstore, {}, {ptr, val}) {}
};

struct InstList : IList<Inst> {
	InstList() = default;
};

struct Region {
	Region(MemArena *arena_) : arena(arena_) {}

	InstList il;

	template <typename T, typename... Args>
	T *Create(Args &&...args)
	{
		auto *mem = arena->Allocate<T>();
		assert(mem);
		auto res = new (mem) T(std::forward<Args>(args)...);
		res->SetId(inst_id_counter++);
		return res;
	}

	template <typename T>
	void insert_back(T *ins)
	{
		il.insert(il.end(), *ins);
	}

private:
	MemArena *arena;
	u32 inst_id_counter{1};
};

struct Builder {
	explicit Builder(Region *cr_) : cr(cr_), it(cr->il.end()) {}
	Builder(Region *cr_, IListIterator<Inst> it_) : cr(cr_), it(it_) {}

	template <typename T, typename... Args>
	T *Create(Args &&...args)
	{
		auto *ins = cr->Create<T>(std::forward<Args>(args)...);
		cr->il.insert(it, *ins);
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
	Region *cr;
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

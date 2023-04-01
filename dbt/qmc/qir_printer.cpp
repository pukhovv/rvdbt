#include "dbt/qmc/qir_printer.h"
#include <sstream>

namespace dbt::qir
{

char const *const op_names[to_underlying(Op::Count)] = {
#define OP(name, cls, flags) [to_underlying(Op::_##name)] = #name,
    QIR_OPS_LIST(OP)
#undef OP
};

char const *const vtype_names[to_underlying(VType::Count)] = {
#define X(name, str) [to_underlying(VType::name)] = #str,
    X(UNDEF, invalid) X(I8, i8) X(I16, i16) X(I32, i32)
#undef X
};

char const *const condcode_names[to_underlying(CondCode::Count)] = {
#define X(name, str) [to_underlying(CondCode::name)] = #str,
    X(EQ, eq) X(NE, ne) X(LE, le) X(LT, lt) X(GE, ge) X(GT, gt) X(LEU, leu) X(LTU, ltu) X(GEU, geu)
	X(GTU, gtu)
#undef X
};

char const *const runtime_stub_names[to_underlying(RuntimeStubId::Count)] = {
#define X(name) [to_underlying(RuntimeStubId::id_##name)] = #name,
    RUNTIME_STUBS(X)
#undef X
};

struct PrinterVisitor : InstVisitor<PrinterVisitor, void> {
private:
	void addsep()
	{
		ss << " ";
	}

	static constexpr char prop_sep = ':';

	void print(VType type)
	{
		ss << prop_sep;
		ss << GetVTypeNameStr(type);
	}

	void print(VSign sgn)
	{
		ss << prop_sep;
		ss << (sgn == VSign::U ? 'u' : 's');
	}

	void print(CondCode cc)
	{
		ss << prop_sep;
		ss << GetCondCodeNameStr(cc);
	}

	void print(VOperand o)
	{
		addsep();
		auto type = o.GetType();
		ss << "[";
		if (o.IsConst()) {
			ss << "$" << std::hex << o.GetConst() << std::dec;
		} else if (o.IsVGPR()) {
			auto vreg = o.GetVGPR();
			auto vinfo = region->GetVRegsInfo();
			if (vinfo->IsGlobal(vreg)) {
				ss << "@" << vinfo->GetGlobalInfo(vreg)->name;
			} else {
				ss << "%" << vreg;
			}
		} else if (o.IsPGPR()) {
			ss << "_" << o.GetPGPR();
		} else if (o.IsSlot()) {
			auto offs = o.GetSlotOffs();
			if (o.IsGSlot()) {
				ss << "g";
			} else {
				ss << "l";
			}
			ss << ":" << std::hex << offs << std::dec;
		} else {
			unreachable("");
		}
		ss << "|" << GetVTypeNameStr(type) << "]";
	}

	void print(Block *b)
	{
		addsep();
		ss << "bb." << b->GetId();
	}

	void printName(Inst *ins)
	{
		ss << "    #" << ins->GetId() << " " << GetOpNameStr(ins->GetOpcode());
	}

	template <typename T>
	inline void printOperands(T *ins)
	{
		// TODO: iterators
		auto out = ins->outputs();
		for (u8 idx = 0; idx < out.size(); ++idx)
			print(out[idx]);
		auto in = ins->inputs();
		for (u8 idx = 0; idx < in.size(); ++idx)
			print(in[idx]);
	}

	Region *region;
	std::stringstream &ss;

public:
	PrinterVisitor(Region *region_, std::stringstream &ss_) : region(region_), ss(ss_) {}

	void visitInst(Inst *ins)
	{
		unreachable("");
	}

	void visitInstUnop(InstUnop *ins)
	{
		printName(ins);
		printOperands(ins);
	}

	void visitInstBinop(InstBinop *ins)
	{
		printName(ins);
		printOperands(ins);
	}

	void visitInstSetcc(InstSetcc *ins)
	{
		printName(ins);
		print(ins->cc);
		printOperands(ins);
	}

	void visitInstBr(InstBr *ins)
	{
		printName(ins);
	}

	void visitInstBrcc(InstBrcc *ins)
	{
		printName(ins);
		print(ins->cc);
		printOperands(ins);
	}

	void visitInstGBr(InstGBr *ins)
	{
		printName(ins);
		print(ins->tpc);
	}

	void visitInstGBrind(InstGBrind *ins)
	{
		printName(ins);
		printOperands(ins);
	}

	void visitInstVMLoad(InstVMLoad *ins)
	{
		printName(ins);
		print(ins->sz);
		print(ins->sgn);
		printOperands(ins);
	}

	void visitInstVMStore(InstVMStore *ins)
	{
		printName(ins);
		print(ins->sz);
		print(ins->sgn);
		printOperands(ins);
	}

	void visitInstHcall(InstHcall *ins)
	{
		printName(ins);
		ss << " [" << GetRuntimeStubName(ins->stub) << "]";
		printOperands(ins);
	}
};

std::string PrinterPass::run(Region *r)
{
	std::stringstream ss;

	ss << "region: n_glob=" << r->GetVRegsInfo()->NumAll() << " n_loc=" << r->GetVRegsInfo()->NumLocals();

	for (auto &bb : r->blist) {
		ss << "\nbb." << bb.GetId() << ":";

		ss << " succs[ ";
		for (auto const &s : bb.GetSuccs()) {
			ss << (*s).GetId() << " ";
		}
		ss << "] preds[ ";
		for (auto const &p : bb.GetPreds()) {
			ss << (*p).GetId() << " ";
		}
		ss << "]";

		auto &ilist = bb.ilist;

		for (auto iit = ilist.begin(); iit != ilist.end(); ++iit) {
			ss << "\n";
			PrinterVisitor vis(r, ss);
			vis.visit(&*iit);
		}
	}

	return ss.str();
}

} // namespace dbt::qir

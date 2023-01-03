#include "dbt/qjit/qir_printer.h"
#include <sstream>

namespace dbt::qir
{

LOG_STREAM(qirprint)

char const *const op_names[to_underlying(Op::Count)] = {
#define OP(name, cls) [to_underlying(Op::_##name)] = #name,
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
    X(EQ, eq) X(NE, ne) X(LT, lt) X(GE, ge) X(LTU, ltu) X(GEU, geu)
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

	template <size_t N_OUT, size_t N_IN>
	void printOperands(InstWithOperands<N_OUT, N_IN> *ins)
	{
		for (auto &o : ins->o)
			print(o);
		for (auto &i : ins->i)
			print(i);
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
		ss << " [" << ins->stub << "]";
		printOperands(ins);
	}
};

// static void BlockPrinter(Block *bb) {}

void PrinterPass::run(Region *r)
{
	std::stringstream ss;

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
			vis.visit(&*iit); // TODO:
		}
	}

	auto str = ss.str();
	log_qirprint.write(str.c_str());
}

} // namespace dbt::qir

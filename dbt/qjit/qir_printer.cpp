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
    X(UNDEF, invalid) X(I32, i32)
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
		if (sep) {
			ss << ", ";
		} else {
			ss << " ";
		}
		sep = true;
	}

	void print(VOperandBase &o)
	{
		if (auto *cnst = as<VConst>(&o)) {
			print(*cnst);
		} else {
			print(*cast<VReg>(&o));
		}
	}

	void print(VConst &c)
	{
		addsep();
		ss << GetVTypeNameStr(c.GetType()) << " 0x" << std::hex << c.GetValue() << std::dec;
	}

	void print(VReg &r)
	{
		addsep();
		ss << GetVTypeNameStr(r.GetType()) << " v" << r.GetIdx();
	}

	void print(Label &l)
	{
		addsep();
		ss << "[" << l.GetInst()->GetId() << "]";
	}

	void printName(Inst *ins)
	{
		ss << "[" << ins->GetId() << "] " << GetOpNameStr(ins->GetOpcode());
	}

	template <size_t N_OUT, size_t N_IN>
	void printOperands(InstWithOperands<N_OUT, N_IN> *ins)
	{
		for (auto &o : ins->o)
			print(o);
		for (auto &i : ins->i)
			print(i.bcls());
	}

	bool sep = false;

public:
	void visitInst(Inst *ins)
	{
		printName(ins);
		ss << " DEFAULT";
	}

	void visitInstLabel(InstLabel *ins)
	{
		printName(ins);
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

	void visitInstBrcc(InstBrcc *ins)
	{
		printName(ins);
		addsep();
		ss << GetCondCodeNameStr(ins->cc);
		print(ins->target);
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
		printOperands(ins);
	}

	void visitInstVMStore(InstVMStore *ins)
	{
		printName(ins);
		printOperands(ins);
	}

	std::stringstream ss;
};

void PrinterPass::run(Region *r)
{
	auto &il = r->il;

	log_qirprint("################################################");

	for (auto it = il.begin(); it != il.end(); ++it) {
		PrinterVisitor vis{};
		vis.visit(&*it);
		log_qirprint(vis.ss.str().c_str());
	}

	log_qirprint("################################################");
}

} // namespace dbt::qir

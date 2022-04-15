#include "dbt/rv32i_decode.h"

namespace dbt::rv32i::insn
{

insn::Op Decode(u32 raw)
{
	insn::DecodeParams insn{raw};
#define OP_ILL return insn::Op::_ill;
#define OP(name) return insn::Op::_##name;
	RV32I_DECODE_SWITCH(insn)
#undef OP_ILL
#undef OP
	unreachable("");
}

static char const *gpr_names[32] = {
    [0] = "zero",  [1] = "  ra",  [2] = "  sp",	 [3] = "  gp",	[4] = "  tp",  [5] = "  t0",  [6] = "  t1",
    [7] = "  t2",  [8] = "  fp",  [9] = "  s1",	 [10] = "  a0", [11] = "  a1", [12] = "  a2", [13] = "  a3",
    [14] = "  a4", [15] = "  a5", [16] = "  a6", [17] = "  a7", [18] = " s2",  [19] = " s3",  [20] = " s4",
    [21] = " s5",  [22] = " s6",  [23] = " s7",	 [24] = " s8",	[25] = " s9",  [26] = " s10", [27] = " s11",
    [28] = " t3",  [29] = " t4",  [30] = " t5",	 [31] = " t6"};

char const *GRPToName(u8 r)
{
	return gpr_names[r];
}

std::ostream &operator<<(std::ostream &o, Base i)
{
	return o;
}
std::ostream &operator<<(std::ostream &o, R i)
{
	return o << gpr_names[i.rd()] << " \t" << gpr_names[i.rs1()] << " \t" << gpr_names[i.rs2()];
}
std::ostream &operator<<(std::ostream &o, I i)
{
	return o << gpr_names[i.rd()] << " \t" << gpr_names[i.rs1()] << " \t" << i.imm();
}
std::ostream &operator<<(std::ostream &o, IS i)
{
	return o << gpr_names[i.rd()] << " \t" << gpr_names[i.rs1()] << " \t" << i.imm();
}
std::ostream &operator<<(std::ostream &o, S i)
{
	return o << gpr_names[i.rs2()] << " \t" << gpr_names[i.rs1()] << " \t" << i.imm();
}
std::ostream &operator<<(std::ostream &o, B i)
{
	return o << gpr_names[i.rs1()] << " \t" << gpr_names[i.rs2()] << " \t" << i.imm();
}
std::ostream &operator<<(std::ostream &o, U i)
{
	return o << gpr_names[i.rd()] << " \t" << i.imm();
}
std::ostream &operator<<(std::ostream &o, J i)
{
	return o << gpr_names[i.rd()] << " \t" << i.imm();
}

} // namespace dbt::rv32i::insn

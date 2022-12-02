#pragma once

#define QIR_DEF_LIST(SOP, COP, GROUP)                                                                        \
	COP(helper, InstHelper)                                                                              \
	COP(br, InstBr)                                                                                      \
	COP(brcc, InstBrcc)                                                                                  \
	COP(gbr, InstGBr)                                                                                    \
	COP(gbrind, InstGBrind)                                                                              \
	COP(vmload, InstVMLoad)                                                                              \
	COP(vmstore, InstVMStore)                                                                            \
	/*COP(setcc, InstSetcc)     */                                                                       \
	/* unary */                                                                                          \
	SOP(mov, InstUnop)                                                                                   \
	GROUP(InstUnop, mov, mov)                                                                            \
	/* binary */                                                                                         \
	SOP(add, InstBinop)                                                                                  \
	SOP(sub, InstBinop)                                                                                  \
	SOP(and, InstBinop)                                                                                  \
	SOP(or, InstBinop)                                                                                   \
	SOP(xor, InstBinop)                                                                                  \
	SOP(sra, InstBinop)                                                                                  \
	SOP(srl, InstBinop)                                                                                  \
	SOP(sll, InstBinop)                                                                                  \
	GROUP(InstBinop, add, sll)

#define QIR_OPS_LIST(OP) QIR_DEF_LIST(OP, OP, EMPTY_MACRO)
#define QIR_SUBOPS_LIST(OP) QIR_DEF_LIST(OP, EMPTY_MACRO, EMPTY_MACRO)
#define QIR_CLSOPS_LIST(OP) QIR_DEF_LIST(EMPTY_MACRO, OP, EMPTY_MACRO)
#define QIR_GROUPS_LIST(GROUP) QIR_DEF_LIST(EMPTY_MACRO, EMPTY_MACRO, GROUP)

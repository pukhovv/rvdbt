#pragma once

#define GUEST_RUNTIME_STUBS(X)                                                                               \
	X(rv32_fence)                                                                                        \
	X(rv32_fencei)                                                                                       \
	X(rv32_ecall)                                                                                        \
	X(rv32_ebreak)                                                                                       \
	X(rv32_lrw)                                                                                          \
	X(rv32_scw)                                                                                          \
	X(rv32_amoswapw)                                                                                     \
	X(rv32_amoaddw)                                                                                      \
	X(rv32_amoxorw)                                                                                      \
	X(rv32_amoandw)                                                                                      \
	X(rv32_amoorw)                                                                                       \
	X(rv32_amominw)                                                                                      \
	X(rv32_amomaxw)                                                                                      \
	X(rv32_amominuw)                                                                                     \
	X(rv32_amomaxuw)

#pragma once

#include "dbt/execute.h"

namespace dbt::jitabi
{

#define HELPER extern "C" NOINLINE __attribute__((used))
#define HELPER_ASM extern "C" NOINLINE __attribute__((used, naked))

namespace ppoint
{
struct BranchSlot {
	void Reset();
	void Link(void *to);

	u8 code[12];
	u32 gip;
} __attribute__((packed));
struct Call64Abs {
	u64 op_mov_imm_rax : 16 = 0xb848;
	u64 imm : 64;
	u64 op_call_rax : 16 = 0xd0ff;
} __attribute__((packed));
static_assert(sizeof(Call64Abs) <= sizeof(BranchSlot::code));
struct Jump64Abs {
	u64 op_mov_imm_rax : 16 = 0xb848;
	u64 imm : 64;
	u64 op_jmp_rax : 16 = 0xe0ff;
} __attribute__((packed));
static_assert(sizeof(Jump64Abs) <= sizeof(BranchSlot::code));
struct Jump32Rel {
	u64 op_jmp_imm : 8 = 0xe9;
	u64 imm : 32;
} __attribute__((packed));
static_assert(sizeof(Jump32Rel) <= sizeof(BranchSlot::code));
}; // namespace ppoint

struct _RetPair {
	void *v0;
	void *v1;
};

HELPER_ASM ppoint::BranchSlot *trampoline_host_to_qjit(CPUState *state, void *vmem, void *tc_ptr);
HELPER_ASM void trampoline_qjit_to_host();
HELPER_ASM void stub_link_branch();
HELPER_ASM void stub_trace();
HELPER _RetPair helper_link_branch(void *p_slot);
HELPER _RetPair helper_brind(CPUState *state, u32 gip);
HELPER void helper_raise();
HELPER void helper_dump_trace(CPUState *state);

namespace ppoint
{

inline void BranchSlot::Reset()
{
	auto *patch = new (&code) Call64Abs();
	patch->imm = (uptr)stub_link_branch;
}

inline void BranchSlot::Link(void *to)
{
	iptr rel = (iptr)to - ((iptr)code + sizeof(Jump32Rel));
	if ((i32)rel == rel) {
		auto *patch = new (&code) Jump32Rel();
		patch->imm = rel;
	} else {
		auto *patch = new (&code) Jump64Abs();
		patch->imm = (uptr)to;
	}
}

} // namespace ppoint

} // namespace dbt::jitabi

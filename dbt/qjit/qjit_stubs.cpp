#include "dbt/execute.h"
#include "dbt/qjit/qjit_stubs.h"
#include "dbt/tcache/tcache.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <ostream>
#include <sys/types.h>
#include <type_traits>

namespace dbt::qjit
{

HELPER_ASM BranchSlot *trampoline_host_to_qjit(CPUState *state, void *vmem, void *tc_ptr)
{
	__asm("pushq	%rbp\n\t"
	      "movq	%rsp, %rbp\n\t"
	      "pushq	%rbx\n\t"
	      "pushq	%r12\n\t"
	      "pushq	%r13\n\t"
	      "pushq	%r14\n\t"
	      "pushq	%r15\n\t"
	      "movq 	%rdi, %r13\n\t" // STATE
	      "movq	%rsi, %r12\n\t" // MEMBASE
	      "subq	$248, %rsp\n\t" // RegAlloc::frame_size
	      "jmpq	*%rdx\n\t");	// tc_ptr
}

HELPER_ASM void trampoline_qjit_to_host()
{
	__asm("addq	$248, %rsp\n\t" // RegAlloc::frame_size
	      "popq	%r15\n\t"
	      "popq	%r14\n\t"
	      "popq	%r13\n\t"
	      "popq	%r12\n\t"
	      "popq	%rbx\n\t"
	      "popq	%rbp\n\t"
	      "retq	\n\t");
}
static_assert(stub_frame_size == 248);

HELPER_ASM void stub_link_branch()
{
	__asm("popq	%rdi\n\t"
	      "callq	helper_link_branch@plt\n\t"
	      "jmpq	*%rdx\n\t");
}

HELPER _RetPair helper_link_branch(void *p_slot)
{
	auto *slot = (BranchSlot *)((uptr)p_slot - sizeof(BranchSlotPatch::Call64Abs));
	auto found = tcache::Lookup(slot->gip);
	if (likely(found)) {
		slot->Link(found->tcode.ptr);
		return {slot, found->tcode.ptr};
	}
	return {slot, (void *)trampoline_qjit_to_host};
}

HELPER _RetPair helper_brind(CPUState *state, u32 gip)
{
	state->ip = gip;
	auto *found = tcache::Lookup(gip);
	if (likely(found)) {
		tcache::OnBrind(found);
		return {nullptr, (void *)found->tcode.ptr};
	}
	return {nullptr, (void *)trampoline_qjit_to_host};
}

HELPER void helper_raise()
{
	RaiseTrap();
}

HELPER_ASM void stub_trace()
{
	__asm("pushq	%rax\n\t"
	      "pushq	%rcx\n\t"
	      "pushq	%rdx\n\t"
	      "pushq	%rbx\n\t"
	      "pushq	%rsp\n\t"
	      "pushq	%rbp\n\t"
	      "pushq	%rsi\n\t"
	      "pushq	%rdi\n\t"
	      "pushq	%r8\n\t"
	      "pushq	%r9\n\t"
	      "pushq	%r10\n\t"
	      "pushq	%r11\n\t"
	      "pushq	%r12\n\t"
	      "pushq	%r13\n\t"
	      "pushq	%r14\n\t"
	      "pushq	%r15\n\t"
	      "movq	%r13, %rdi\n\t" // STATE
	      "subq	$8, %rsp\n\t"
	      "callq	helper_dump_trace@plt\n\t"
	      "addq	$8, %rsp\n\t"
	      "popq	%r15\n\t"
	      "popq	%r14\n\t"
	      "popq	%r13\n\t"
	      "popq	%r12\n\t"
	      "popq	%r11\n\t"
	      "popq	%r10\n\t"
	      "popq	%r9\n\t"
	      "popq	%r8\n\t"
	      "popq	%rdi\n\t"
	      "popq	%rsi\n\t"
	      "popq	%rbp\n\t"
	      "popq	%rsp\n\t"
	      "popq	%rbx\n\t"
	      "popq	%rdx\n\t"
	      "popq	%rcx\n\t"
	      "popq	%rax\n\t"
	      "retq	\n\t");
}

HELPER void helper_dump_trace(CPUState *state)
{
	state->DumpTrace("entry");
}

} // namespace dbt::qjit

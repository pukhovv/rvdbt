#include "dbt/qjit/qcg/jitabi.h"
#include "dbt/qjit/qcg/arch_traits.h"
#include "dbt/tcache/tcache.h"

namespace dbt::jitabi
{

#define HELPER extern "C" NOINLINE __attribute__((used))
#define HELPER_ASM extern "C" NOINLINE __attribute__((used, naked))

HELPER_ASM ppoint::BranchSlot *trampoline_to_jit(CPUState *state, void *vmem, void *tc_ptr)
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
	      "subq	$248, %rsp\n\t" // stub_frame_size
	      "jmpq	*%rdx\n\t");	// tc_ptr
}

HELPER_ASM void qcgstub_escape()
{
	__asm("addq	$248, %rsp\n\t" // stub_frame_size
	      "popq	%r15\n\t"
	      "popq	%r14\n\t"
	      "popq	%r13\n\t"
	      "popq	%r12\n\t"
	      "popq	%rbx\n\t"
	      "popq	%rbp\n\t"
	      "retq	\n\t");
}
static_assert(qcg::ArchTraits::STATE == asmjit::x86::Gp::kIdR13);
static_assert(qcg::ArchTraits::MEMBASE == asmjit::x86::Gp::kIdR12);
static_assert(qcg::ArchTraits::SP == asmjit::x86::Gp::kIdSp);
static_assert(qcg::stub_frame_size == 248);

// Caller uses 2nd value in returned pair as jump target
static ALWAYS_INLINE _RetPair TryLinkBranch(ppoint::BranchSlot *slot)
{
	auto found = tcache::Lookup(slot->gip);
	if (likely(found)) {
		slot->Link(found->tcode.ptr);
		return {slot, found->tcode.ptr};
	}
	return {slot, (void *)qcgstub_escape};
}

HELPER_ASM void qcgstub_link_branch_jit()
{
	__asm("popq	%rdi\n\t"
	      "callq	qcg_TryLinkBranchJIT@plt\n\t"
	      "jmpq	*%rdx\n\t");
}

HELPER_ASM void qcgstub_link_branch_aot()
{
	__asm("popq	%rdi\n\t"
	      "callq	qcg_TryLinkBranchAOT@plt\n\t"
	      "jmpq	*%rdx\n\t");
}

HELPER _RetPair qcg_TryLinkBranchJIT(void *retaddr)
{
	return TryLinkBranch(ppoint::BranchSlot::FromCallPtrRetaddr(retaddr));
}

HELPER _RetPair qcg_TryLinkBranchAOT(void *retaddr)
{
	return TryLinkBranch(ppoint::BranchSlot::FromCallRuntimeStubRetaddr(retaddr));
}

HELPER _RetPair qcgstub_brind(CPUState *state, u32 gip)
{
	state->ip = gip;
	auto *found = tcache::Lookup(gip);
	if (likely(found)) {
		tcache::CacheBrind(found);
		return {nullptr, (void *)found->tcode.ptr};
	}
	return {nullptr, (void *)qcgstub_escape};
}

HELPER void qcgstub_raise()
{
	RaiseTrap();
}

#define PUSH_NONCSR_GPR()                                                                                    \
	"pushq	%rax\n\t"                                                                                     \
	"pushq	%rcx\n\t"                                                                                     \
	"pushq	%rdx\n\t"                                                                                     \
	"pushq	%rsi\n\t"                                                                                     \
	"pushq	%rdi\n\t"                                                                                     \
	"pushq	%r8\n\t"                                                                                      \
	"pushq	%r9\n\t"                                                                                      \
	"pushq	%r10\n\t"                                                                                     \
	"pushq	%r11\n\t"

#define POP_NONCSR_GPR()                                                                                     \
	"popq	%r11\n\t"                                                                                      \
	"popq	%r10\n\t"                                                                                      \
	"popq	%r9\n\t"                                                                                       \
	"popq	%r8\n\t"                                                                                       \
	"popq	%rdi\n\t"                                                                                      \
	"popq	%rsi\n\t"                                                                                      \
	"popq	%rdx\n\t"                                                                                      \
	"popq	%rcx\n\t"                                                                                      \
	"popq	%rax\n\t"

HELPER_ASM void qcgstub_trace()
{
	__asm(PUSH_NONCSR_GPR());

	__asm("pushq	%r13\n\t"
	      "movq	%r13, %rdi\n\t" // STATE
	      "subq	$8, %rsp\n\t"
	      "callq	qcg_DumpTrace@plt\n\t"
	      "addq	$8, %rsp\n\t"
	      "popq	%r13\n\t");

	__asm(POP_NONCSR_GPR());
	__asm("retq	\n\t");
}
static_assert(qcg::ArchTraits::STATE == asmjit::x86::Gp::kIdR13);

HELPER void qcg_DumpTrace(CPUState *state)
{
	state->DumpTrace("entry");
}

} // namespace dbt::jitabi

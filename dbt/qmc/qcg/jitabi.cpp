#include "dbt/qmc/qcg/jitabi.h"
#include "dbt/execute.h"
#include "dbt/qmc/qcg/arch_traits.h"
#include "dbt/tcache/tcache.h"

namespace dbt::jitabi
{

struct _RetPair {
	void *v0;
	void *v1;
};

#define HELPER extern "C" NOINLINE __attribute__((used))
#define HELPER_ASM extern "C" NOINLINE __attribute__((used, naked))

/*    qmc qcg/llvm frame layout, grows down
 *
 *			| ....		|  Execution loop
 *	trampoline call +---------------+-----------------------
 *			| link+fp|saved |  qcg spill frame, created in trampoline
 *			+---------------+  no callee saved regs expected
 *			| qcg locals	|  returning to this frame is not allowed
 *  	       tailcall +---------------+-----------------------
 *			| link+pad  	|  Translated region frame
 *			+---------------|  Destroyed on branch to next region
 *			| llvm locals	|  qcg/ghccc callconv doesn't preserve fp
 *   abs/qcg-reloc call +---------------+-----------------------
 *			| ....		|  qcgstub_* frame
 */

static_assert((u64(QCG_SPILL_FRAME_SIZE) & 15) == 0);
static_assert(qcg::ArchTraits::STATE == asmjit::x86::Gp::kIdR13);
static_assert(qcg::ArchTraits::MEMBASE == asmjit::x86::Gp::kIdBp);

// Build qcg spillframe and enter translated code
HELPER_ASM ppoint::BranchSlot *trampoline_to_jit(CPUState *state, void *vmem, void *tc_ptr)
{
	asm("pushq	%rbp\n\t"
	    "pushq	%rbx\n\t"
	    "pushq	%r12\n\t"
	    "pushq	%r13\n\t"
	    "pushq	%r14\n\t"
	    "pushq	%r15\n\t"
	    "movq 	%rdi, %r13\n\t"	  // STATE
	    "movq	%rsi, %rbp\n\t"); // MEMBASE
	asm("sub     $" STRINGIFY(QCG_SPILL_FRAME_SIZE + 8) ", %rsp\n\t");
	asm("callq	*%rdx\n\t" // tc_ptr
	    "int	$3");	   // use escape/raise stub instead
}

// Escape from translated code, forward `rax` to caller
HELPER_ASM void qcgstub_escape()
{
	asm("addq   $(" STRINGIFY(QCG_SPILL_FRAME_SIZE + 16) "), %rsp\n\t");
	asm("popq	%r15\n\t"
	    "popq	%r14\n\t"
	    "popq	%r13\n\t"
	    "popq	%r12\n\t"
	    "popq	%rbx\n\t"
	    "popq	%rbp\n\t"
	    "retq	\n\t");
}

// Caller uses 2nd value in returned pair as jump target
static ALWAYS_INLINE _RetPair TryLinkBranch(CPUState *state, ppoint::BranchSlot *slot)
{
	auto found = tcache::Lookup(slot->gip);
	if (likely(found)) {
		slot->Link(found->tcode.ptr);
		tcache::RecordLink(slot, found, slot->flags.cross_segment);
		return {slot, found->tcode.ptr};
	}
	state->ip = slot->gip;
	return {slot, (void *)qcgstub_escape};
}

// Lazy region linking, absolute call target (jit/aot mode)
HELPER_ASM void qcgstub_link_branch_jit()
{
	asm("movq	0(%rsp), %rsi\n\t"
	    "movq	%r13, %rdi\n\t"
	    "callq	qcg_TryLinkBranchJIT@plt\n\t"
	    "popq	%rdi\n\t" // pop somewhere
	    "jmpq	*%rdx\n\t");
}

// Lazy region linking, qcg-relocation call target (aot mode)
HELPER_ASM void qcgstub_link_branch_aot()
{
	asm("movq	0(%rsp), %rsi\n\t"
	    "movq	%r13, %rdi\n\t"
	    "callq	qcg_TryLinkBranchAOT@plt\n\t"
	    "popq	%rdi\n\t" // pop somewhere
	    "jmpq	*%rdx\n\t");
}

HELPER _RetPair qcg_TryLinkBranchJIT(CPUState *state, void *retaddr)
{
	return TryLinkBranch(state, ppoint::BranchSlot::FromCallPtrRetaddr(retaddr));
}

HELPER _RetPair qcg_TryLinkBranchAOT(CPUState *state, void *retaddr)
{
	return TryLinkBranch(state, ppoint::BranchSlot::FromCallRuntimeStubRetaddr(retaddr));
}

// Indirect branch slowpath
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
	asm(PUSH_NONCSR_GPR());

	asm("pushq	%r13\n\t"
	    "movq	%r13, %rdi\n\t" // STATE
	    "subq	$8, %rsp\n\t"
	    "callq	qcg_DumpTrace@plt\n\t"
	    "addq	$8, %rsp\n\t"
	    "popq	%r13\n\t");

	asm(POP_NONCSR_GPR());
	asm("retq	\n\t");
}
static_assert(qcg::ArchTraits::STATE == asmjit::x86::Gp::kIdR13);

HELPER void qcg_DumpTrace(CPUState *state)
{
	state->DumpTrace("entry");
}

} // namespace dbt::jitabi

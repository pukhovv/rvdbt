#pragma once

#include "dbt/common.h"
#include "dbt/guest/rv32_cpu.h"

namespace dbt
{

struct ukernel {
	struct ElfImage {
		u32 entry;
		u32 stack_start;
	};

	static void LoadElf(char const *path, ElfImage *img);
	static void InitThread(CPUState *state, ElfImage *elf);
	static void Syscall(CPUState *state);
	static void Execute(CPUState *state);

private:
	ukernel() {}
};

} // namespace dbt

#pragma once

#include "dbt/common.h"
#include "dbt/rv32i_runtime.h"

namespace dbt
{

struct ukernel {
	struct ElfImage {
		u32 entry;
		u32 stack_start;
	};

	static void LoadElf(char const *path, ElfImage *img);
	static void InitThread(rv32i::CPUState *state, ElfImage *elf);
	static void Syscall(rv32i::CPUState *state);
	static void Execute(rv32i::CPUState *state);

private:
	ukernel() {}
};

} // namespace dbt

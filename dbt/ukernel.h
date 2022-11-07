#pragma once

#include "dbt/guest/rv32_cpu.h"

extern "C" {
#include <elf.h>
};

namespace dbt
{

struct ukernel {
	struct ElfImage {
		Elf32_Ehdr ehdr;
		u32 load_addr;
		u32 stack_start;
		u32 entry;
		u32 brk;
	};

	void LoadElf(char const *path, ElfImage *img);
	void InitAVectors(ElfImage *elf, int argv_n, char **argv);
	static void InitThread(CPUState *state, ElfImage *elf);

	void Execute(CPUState *state);

	static void SyscallDemo(CPUState *state);
	void SyscallLinux(CPUState *state);
	int PathResolution(int dirfd, char const *path, char *resolved);

	u32 do_sys_brk(u32 newbrk);

private:
	u32 brk{};
};

} // namespace dbt

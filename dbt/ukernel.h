#pragma once

#include "dbt/guest/rv32_cpu.h"

namespace dbt
{

// TODO: add guardians for g2h
#define __user

using uabi_short = i32;
using uabi_ushort = u32;
using uabi_int = i32;
using uabi_uint = u32;
using uabi_long = i32;
using uabi_ulong = u32;
using uabi_size_t = u32;

struct ukernel {
	struct ElfImage;
	struct Process;

	void SetFSRoot(char const *fsroot_);

	void BootElf(char const *path, ElfImage *elf);
	static void ReproduceElf(char const *path, ElfImage *elf);

	void InitAVectors(ElfImage *elf, int argv_n, char **argv);
	static void InitThread(CPUState *state, ElfImage *elf);
	static void InitSignals(CPUState *state);

	int Execute(CPUState *state);

	static void SyscallDemo(CPUState *state);
	void SyscallLinux(CPUState *state);

	static ElfImage exe_elf_image;
	static Process process;

private:
	static void LoadElf(int elf_fd, ElfImage *elf);
};

} // namespace dbt

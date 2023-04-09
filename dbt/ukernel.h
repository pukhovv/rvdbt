#pragma once

#include "dbt/guest/rv32_cpu.h"

namespace dbt
{

struct ukernel {
	struct ElfImage;

	void SetFSRoot(char const *fsroot_);

	void BootElf(char const *path, ElfImage *elf);
	static void ReproduceElf(char const *path, ElfImage *elf);

	void InitAVectors(ElfImage *elf, int argv_n, char **argv);
	static void InitThread(CPUState *state, ElfImage *elf);

	int Execute(CPUState *state);

	static void SyscallDemo(CPUState *state);
	void SyscallLinux(CPUState *state);
	int PathResolution(int dirfd, char const *path, char *resolved);
	int HandleSpecialPath(char const *path, char *resolved);

	u32 do_sys_brk(u32 newbrk);

	static ElfImage exe_elf_image;

private:
	static void LoadElf(int elf_fd, ElfImage *elf);

	std::string fsroot;
	int exe_fd{-1};
	u32 brk{};
};

} // namespace dbt

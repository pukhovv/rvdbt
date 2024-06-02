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
using uabi_llong = i64;
using uabi_ullong = u64;
using uabi_size_t = u32;

struct uthread;

struct ukernel {
	ukernel() = delete;

	struct ElfImage;
	struct Process;

	static void SetFSRoot(char const *fsroot_);

	static void MainThreadBoot(int argv_n, char **argv);
	static int MainThreadExecute();

	static void ReproduceElfMappings(char const *path);

	static void Execute();
	static void EnqueueTermination(int code);

	static Process process;

private:
	static void InitElfMappings(char const *path, ElfImage *elf);
	static void InitAVectors(ElfImage *elf, int argv_n, char **argv);
	static void LoadElf(int elf_fd, ElfImage *elf);

	static void InitMainThread(CPUState *state, ElfImage *elf);
	static void InitSignals(CPUState *state);
	static void Syscall(CPUState *state);
	static void SyscallDemo(CPUState *state);
	static void SyscallLinux(CPUState *state);

	friend struct uthread;
};

} // namespace dbt

#include "dbt/ukernel.h"
#include "dbt/execute.h"
#include "dbt/mmu.h"
#include "dbt/tcache/objprof.h"
#include <alloca.h>
#include <cstring>

extern "C" {
#include <elf.h>
#include <fcntl.h>
#include <libgen.h>
#include <linux/limits.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>
};

namespace dbt
{
LOG_STREAM(ukernel);

struct ukernel::ElfImage {
	Elf32_Ehdr ehdr;
	u32 load_addr;
	u32 stack_start;
	u32 entry;
	u32 brk;
};
ukernel::ElfImage ukernel::exe_elf_image{};

int ukernel::Execute(CPUState *state)
{
	CPUState::SetCurrent(state);
	while (true) {
		dbt::Execute(state);
		switch (state->trapno) {
		case rv32::TrapCode::EBREAK:
			log_ukernel("ebreak termiante");
			return 1;
		case rv32::TrapCode::ECALL:
			state->ip += 4;
#ifdef CONFIG_LINUX_GUEST
			ukernel::SyscallLinux(state);
#else
			ukernel::SyscallDemo(state);
#endif
			if (state->trapno == rv32::TrapCode::TERMINATED) {
				log_ukernel("exiting...");
				return state->gpr[10]; // TODO: forward sys_exit* arg
			}
			break;
		case rv32::TrapCode::ILLEGAL_INSN:
			log_ukernel("illegal instruction at %08x", state->ip);
			return 1;
		default:
			unreachable("no handle for trap");
		}
	}
	CPUState::SetCurrent(nullptr);
}

void ukernel::InitThread(CPUState *state, ElfImage *elf)
{
	assert(!(elf->stack_start & 15));
	state->gpr[2] = elf->stack_start;
	state->ip = elf->entry;
}

static void dbt_sigaction_memory(int signo, siginfo_t *sinfo, void *uctx_raw)
{
	auto uc = static_cast<ucontext_t *>(uctx_raw);

	auto hpc = uc->uc_mcontext.gregs[REG_RIP];

	log_ukernel("dbt_sigaction_memory:host: signal=%d, pc=%p, si_addr=%p", signo, hpc, sinfo->si_addr);
	if (!mmu::check_h2g(sinfo->si_addr)) {
		Panic("Memory fault in host address space!!!");
	}
	auto g_faddr = mmu::h2g(sinfo->si_addr);

	auto state = CPUState::Current();
	state->DumpTrace("signal");
	log_ukernel("\tfault:guest: pc=%08x, si_addr=%08x", state->ip, g_faddr);
	Panic("Memory fault in guest address space. See logs for more details");
}

// TODO: emulate signals
void ukernel::InitSignals(CPUState *state)
{
	struct sigaction sa;
	sigset_t sset;

	sigfillset(&sset);
	sa.sa_flags = SA_SIGINFO;
	sa.sa_sigaction = dbt_sigaction_memory;

	sigaction(SIGSEGV, &sa, nullptr);
	sigaction(SIGBUS, &sa, nullptr);
}

void ukernel::SyscallDemo(CPUState *state)
{
	state->trapno = rv32::TrapCode::NONE;
	u32 id = state->gpr[10];
	switch (id) {
	case 1: {
		log_ukernel("syscall readnum");
		u32 res;
		fscanf(stdin, "%d", &res);
		state->gpr[10] = res;
		return;
	}
	case 2: {
		log_ukernel("syscall writenum");
		fprintf(stdout, "%d\n", state->gpr[11]);
		return;
	}
	default:
		Panic("unknown syscall");
	}
}

void ukernel::SyscallLinux(CPUState *state)
{
	state->trapno = rv32::TrapCode::NONE;
	u32 args[7] = {state->gpr[10], state->gpr[11], state->gpr[12], state->gpr[13],
		       state->gpr[14], state->gpr[15], state->gpr[16]};
	u32 syscallno = state->gpr[17];
	i32 rc;

#define RCERRNO(rc)                                                                                          \
	do {                                                                                                 \
		if ((rc) < 0)                                                                                \
			(rc) = -errno;                                                                       \
	} while (0)

#define DEF_SYSCALL(no, name)                                                                                \
	break;                                                                                               \
	case no:                                                                                             \
		log_ukernel("sys_%s (no=%d)\t ip=%08x", #name, syscallno, state->ip);

	switch (syscallno) {
		DEF_SYSCALL(56, openat)
		{
			rc = openat(args[0], (char const *)mmu::g2h(args[1]), args[2], args[3]);
			RCERRNO(rc);
		}
		DEF_SYSCALL(57, close)
		{
			if (args[0] < 3) { // TODO: split file descriptors
				rc = 0;
				break;
			}
			rc = close(args[0]);
			RCERRNO(rc);
		}
		DEF_SYSCALL(62, lseek)
		{
			// asmlinkage long sys_llseek(unsigned int fd, unsigned long offset_high, unsigned
			// long offset_low, loff_t __user *result, unsigned int whence);
			off_t off = ((u64)args[1] << 32) | args[2];
			rc = lseek(args[0], off, args[4]);
			if (rc < 0) {
				(rc) = -errno;
			} else {
				*(u64 *)mmu::g2h(args[3]) = rc;
			}
		}
		DEF_SYSCALL(63, read)
		{
			rc = read(args[0], mmu::g2h(args[1]), args[2]);
			RCERRNO(rc);
		}
		DEF_SYSCALL(64, write)
		{
			rc = write((i32)args[0], mmu::g2h(args[1]), args[2]);
			RCERRNO(rc);
		}
		DEF_SYSCALL(78, readlinkat)
		{
			char pathbuf[PATH_MAX];
			char const *path = (char *)mmu::g2h(args[1]);
			if (*path) {
				rc = PathResolution((i32)args[0], path, pathbuf);
				if (rc < 0) {
					rc = -errno;
					break;
				}
			} else {
				pathbuf[0] = 0;
			}
			rc = readlinkat((i32)args[0], pathbuf, (char *)mmu::g2h(args[2]), args[3]);
			RCERRNO(rc);
		}
		DEF_SYSCALL(80, newfstat)
		{
			rc = fstatat((i32)args[0], "", (struct stat *)mmu::g2h(args[1]), 0);
			RCERRNO(rc);
		}
		DEF_SYSCALL(93, exit)
		{
			rc = args[0];
			state->trapno = rv32::TrapCode::TERMINATED;
		}
		DEF_SYSCALL(94, exit_group)
		{
			rc = args[0];
			state->trapno = rv32::TrapCode::TERMINATED;
		}
		DEF_SYSCALL(134, rt_sigaction)
		{
			rc = 0;
			log_ukernel("TODO: support signals!");
		}
		DEF_SYSCALL(160, uname)
		{
			auto *un = (struct utsname *)mmu::g2h(args[0]);
			rc = uname(un);
			strcpy(un->machine, "riscv32");
			RCERRNO(rc);
		}
		DEF_SYSCALL(174, getuid)
		{
			rc = getuid();
			RCERRNO(rc);
		}
		DEF_SYSCALL(175, geteuid)
		{
			rc = geteuid();
			RCERRNO(rc);
		}
		DEF_SYSCALL(176, getgid)
		{
			rc = getgid();
			RCERRNO(rc);
		}
		DEF_SYSCALL(177, getegid)
		{
			rc = getegid();
			RCERRNO(rc);
		}
		DEF_SYSCALL(179, sysinfo)
		{
			struct rv32abi_sysinfo {
				u32 uptime;
				u32 loads[3];
				u32 totalram;
				u32 freeram;
				u32 sharedram;
				u32 bufferram;
				u32 totalswap;
				u32 freeswap;
				u16 procs;
				u16 pad;
				u32 totalhigh;
				u32 freehigh;
				u32 mem_unit;
				char _f[20 - 2 * sizeof(u32) - sizeof(u32)];
			};
			struct sysinfo hs;
			rc = sysinfo(&hs);

			if (rc > 0) {
				auto *gs = (struct rv32abi_sysinfo *)mmu::g2h(args[0]);
				gs->uptime = hs.uptime;
				for (int i = 0; i < 3; ++i) {
					gs->loads[i] = hs.loads[i];
				}
				gs->totalram = 1_GB;
				gs->freeram = 500_MB;
				gs->sharedram = gs->bufferram = gs->totalswap = gs->freeswap = 1_MB;
				gs->procs = hs.procs;
				gs->totalhigh = gs->freehigh = 1_MB;
				gs->mem_unit = 1;
			}

			RCERRNO(rc);
		}
		DEF_SYSCALL(214, brk)
		{
			rc = do_sys_brk(args[0]);
		}
		DEF_SYSCALL(215, munmap)
		{
			// TODO: implement in mmu
			log_ukernel("munmap addr: %x", mmu::g2h(args[0]));
			rc = munmap(mmu::g2h(args[0]), args[1]);
			RCERRNO(rc);
		}
		DEF_SYSCALL(222, mmap)
		{
			// TODO: file maps in mmu
			void *ret = mmu::mmap(args[0], args[1], args[2], args[3], args[4], args[5]);
			if (ret == MAP_FAILED) {
				rc = -errno;
				break;
			}
			rc = mmu::h2g(ret);
			log_ukernel("mmap addr: %x", rc);
		}
		DEF_SYSCALL(226, mprotect)
		{
			// TODO: implement in mmu
			rc = mprotect(mmu::g2h(args[0]), args[1], args[2]);
			RCERRNO(rc);
		}
		DEF_SYSCALL(291, statx)
		{
			char pathbuf[PATH_MAX];
			char const *path = (char *)mmu::g2h(args[1]);
			if (*path) {
				rc = PathResolution((i32)args[0], path, pathbuf);
				if (rc < 0) {
					rc = -errno;
				}
			} else {
				pathbuf[0] = 0;
			}
			rc =
			    statx((i32)args[0], pathbuf, args[2], args[3], (struct statx *)mmu::g2h(args[4]));
			RCERRNO(rc);
		}
		DEF_SYSCALL(403, clock_gettime)
		{
			rc = clock_gettime(args[0], (timespec *)mmu::g2h(args[1]));
			RCERRNO(rc);
		}
		break;
	default:
		log_ukernel("sys_ (no=%4d) is unknown", syscallno);
		Panic("unknown syscall");
	}
	log_ukernel("    ret: %d", rc);
	state->gpr[10] = rc;
}

int ukernel::HandleSpecialPath(char const *path, char *resolved)
{
	if (!strcmp(path, "/proc/self/exe")) {
		sprintf(resolved, "/proc/self/fd/%d", exe_fd);
		log_ukernel("exe_fd: %s", resolved);
		return 1;
	}
	return 0;
}

void ukernel::SetFSRoot(const char *fsroot_)
{
	char buf[PATH_MAX];
	if (!realpath(fsroot_, buf)) {
		Panic("failed to resolve fsroot");
	}
	fsroot = std::string(buf) + "/";
}

int ukernel::PathResolution(int dirfd, char const *path, char *resolved)
{
	char rp_buf[PATH_MAX];

	log_ukernel("start path resolution: %s", path);

	if (path[0] == '/') {
		snprintf(rp_buf, sizeof(rp_buf), "%s/%s", fsroot.c_str(), path);
	} else {
		if (dirfd == AT_FDCWD) {
			getcwd(rp_buf, sizeof(rp_buf));
		} else {
			char fdpath[64];
			sprintf(fdpath, "/proc/self/fd/%d", dirfd);
			if (readlink(fdpath, rp_buf, sizeof(rp_buf)) < 0) {
				log_ukernel("bad dirfd");
				return -1;
			}
		}
		size_t pref_sz = strlen(rp_buf);
		strncpy(rp_buf + pref_sz, path, sizeof(rp_buf) - pref_sz);
	}
	if (strncmp(rp_buf, fsroot.c_str(), fsroot.length())) {
		Panic("escaped fsroot");
	}

	if (HandleSpecialPath(rp_buf + fsroot.length() + 1, resolved) > 0) {
		return 0;
	}

	// TODO: make it preceise, resolve "/.." and symlinks
	if (!realpath(rp_buf, resolved)) {
		log_ukernel("unresolved path %s", rp_buf);
		return -1;
	}
	if (strncmp(resolved, fsroot.c_str(), fsroot.length())) {
		Panic("escaped fsroot");
	}

	if (path[0] != '/') {
		strcpy(resolved, path);
	}

	return 0;
}

u32 ukernel::do_sys_brk(u32 newbrk)
{
	if (newbrk <= brk) {
		log_ukernel("do_sys_brk: newbrk is too small: %08x %08x", newbrk, brk);
		return brk;
	}
	u32 brk_p = roundup(brk, mmu::PAGE_SIZE);
	if (newbrk <= brk_p) {
		if (newbrk != brk_p) {
			memset(mmu::g2h(brk), 0, newbrk - brk);
		}
		return brk = newbrk;
	}
	void *mem =
	    mmu::mmap(brk_p, newbrk - brk_p, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE | MAP_FIXED);
	if (mmu::h2g(mem) != brk_p) {
		log_ukernel("do_sys_brk: not enough mem");
		munmap(mem, newbrk - brk_p);
		return brk;
	}
	memset(mmu::g2h(brk), 0, brk_p - brk);
	return brk = newbrk;
}

void ukernel::BootElf(const char *path, ElfImage *elf)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		Panic("no such elf file");
	}

	{ // TODO: for AT_FDCWD resolution, remove it
		char buf[PATH_MAX];
		strncpy(buf, path, sizeof(buf));
		chdir(dirname(buf));
	}

	LoadElf(fd, elf);
	objprof::Open(fd, true);
	exe_fd = fd;
	brk = elf->brk; // TODO: move it out

	static constexpr u32 stk_size = 8_MB; // switch to 32 * mmu::PAGE_SIZE if debugging
#if 0
	void *stk_ptr = mmu::MMap(0, stk_size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE);
#else
	[[maybe_unused]] void *stk_guard = mmu::mmap(mmu::ASPACE_SIZE - mmu::PAGE_SIZE, mmu::PAGE_SIZE, 0,
						     MAP_ANON | MAP_PRIVATE | MAP_FIXED);
	// ASAN somehow breaks MMap lookup if it's not MAP_FIXED
	void *stk_ptr = mmu::mmap(mmu::ASPACE_SIZE - mmu::PAGE_SIZE - stk_size, stk_size,
				  PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE | MAP_FIXED);
#endif
	elf->stack_start = mmu::h2g(stk_ptr) + stk_size;
}

void ukernel::ReproduceElf(const char *path, ElfImage *elf)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		Panic("no such elf file");
	}
	LoadElf(fd, elf);
	objprof::Open(fd, false);
	close(fd);
}

// -march=rv32i -O2 -fpic -fpie -static
// -march=rv32i -O2 -fpic -fpie -static -ffreestanding -nostartfiles -nolibc
void ukernel::LoadElf(int fd, ElfImage *elf)
{
	auto &ehdr = elf->ehdr;

	if (pread(fd, &elf->ehdr, sizeof(ehdr), 0) != sizeof(ehdr)) {
		Panic("can't read elf header");
	}
	if (memcmp(ehdr.e_ident,
		   "\x7f"
		   "ELF",
		   4)) {
		Panic("that's not elf");
	}
	if (ehdr.e_machine != EM_RISCV) {
		Panic("elf's machine doesn't match");
	}
	if (ehdr.e_type != ET_EXEC) {
		Panic("unuspported elf type");
	}

	ssize_t phtab_sz = sizeof(Elf32_Phdr) * ehdr.e_phnum;
	auto *phtab = (Elf32_Phdr *)alloca(phtab_sz);
	if (pread(fd, phtab, phtab_sz, ehdr.e_phoff) != phtab_sz) {
		Panic("can't read phtab");
	}
	elf->load_addr = -1;
	elf->brk = 0;
	elf->entry = elf->ehdr.e_entry;

	for (size_t i = 0; i < ehdr.e_phnum; ++i) {
		auto *phdr = &phtab[i];
		if (phdr->p_type != PT_LOAD)
			continue;

		int prot = 0;
		if (phdr->p_flags & PF_R) {
			prot |= PROT_READ;
		}
		if (phdr->p_flags & PF_W) {
			prot |= PROT_WRITE;
		}
		if (phdr->p_flags & PF_X) {
			prot |= PROT_EXEC;
		}

		auto vaddr = phdr->p_vaddr;
		auto vaddr_ps = rounddown(phdr->p_vaddr, mmu::PAGE_SIZE);
		auto vaddr_po = vaddr - vaddr_ps;

		if (phdr->p_filesz != 0) {
			u32 len = roundup(phdr->p_filesz + vaddr_po, mmu::PAGE_SIZE);
			// shared flags
			mmu::mmap(vaddr_ps, len, prot, MAP_FIXED | MAP_PRIVATE, fd,
				  phdr->p_offset - vaddr_po);
			if (phdr->p_memsz > phdr->p_filesz) {
				auto bss_start = vaddr + phdr->p_filesz;
				auto bss_end = vaddr_ps + phdr->p_memsz;
				auto bss_start_nextp = roundup(bss_start, (u32)mmu::PAGE_SIZE);
				auto bss_len = roundup(bss_end - bss_start, (u32)mmu::PAGE_SIZE);
				mmu::mmap(bss_start_nextp, bss_len, prot, MAP_FIXED | MAP_PRIVATE | MAP_ANON);
				u32 prev_sz = bss_start_nextp - bss_start;
				if (prev_sz != 0) {
					memset(mmu::g2h(bss_start), 0, prev_sz);
				}
			}
		} else if (phdr->p_memsz != 0) {
			u32 len = roundup(phdr->p_memsz + vaddr_po, (u32)mmu::PAGE_SIZE);
			mmu::mmap(vaddr_ps, len, prot, MAP_FIXED | MAP_PRIVATE | MAP_ANON);
		}

		elf->load_addr = std::min(elf->load_addr, vaddr - phdr->p_offset);
		elf->brk = std::max(elf->brk, vaddr + phdr->p_memsz);
	}
}

static u32 AllocAVectorStr(u32 stk, void const *str, u16 sz)
{
	stk -= sz;
	memcpy(mmu::g2h(stk), str, sz);
	return stk;
}

static inline u32 AllocAVectorStr(u32 stk, char const *str)
{
	return AllocAVectorStr(stk, str, strlen(str) + 1);
}

// TODO: refactor
void ukernel::InitAVectors(ElfImage *elf, int argv_n, char **argv)
{
	u32 stk = elf->stack_start;

	u32 foo_str_g = stk = AllocAVectorStr(stk, "__foo_str__");
	u32 lc_all_str_g = stk = AllocAVectorStr(stk, "LC_ALL=C");
	char auxv_salt[16] = {0, 1, 2, 3, 4, 5, 6};
	u32 auxv_salt_g = stk = AllocAVectorStr(stk, auxv_salt, sizeof(auxv_salt));

	u32 *argv_strings_g = (u32 *)alloca(sizeof(char *) * argv_n);
	for (int i = 0; i < argv_n; ++i) {
		argv_strings_g[i] = stk = AllocAVectorStr(stk, argv[i]);
	}

	stk &= -4;

	int envp_n = 1;
	int auxv_n = 64;

	int stk_vsz = argv_n + envp_n + auxv_n + 3;
	stk -= stk_vsz * sizeof(u32);
	stk &= -16;
	u32 argc_p = stk;
	u32 argv_p = argc_p + sizeof(u32);
	u32 envp_p = argv_p + sizeof(u32) * (argv_n + 1);
	u32 auxv_p = envp_p + sizeof(u32) * (envp_n + 1);

	auto push_avval = [](uint32_t &vec, uint32_t val) {
		*(u32 *)mmu::g2h(vec) = (val);
		vec += sizeof(u32);
	};
	auto push_auxv = [&](uint16_t idx, uint32_t val) {
		push_avval(auxv_p, idx);
		push_avval(auxv_p, val);
		/* log_ukernel("put auxv %08x=%08x", (idx), (val)); */
	};

	push_avval(argc_p, argv_n);

	for (int i = 0; i < argv_n; ++i) {
		push_avval(argv_p, argv_strings_g[i]);
	}
	push_avval(argv_p, 0);

	push_avval(envp_p, lc_all_str_g);
	push_avval(envp_p, 0);

	push_auxv(AT_PHDR, elf->ehdr.e_phoff + elf->load_addr);
	push_auxv(AT_PHENT, sizeof(Elf32_Phdr));
	push_auxv(AT_PHNUM, elf->ehdr.e_phnum);
	push_auxv(AT_PAGESZ, mmu::PAGE_SIZE);
	push_auxv(AT_BASE, 0);
	push_auxv(AT_FLAGS, 0);
	push_auxv(AT_ENTRY, elf->entry);

	push_auxv(AT_UID, getuid());
	push_auxv(AT_GID, getgid());
	push_auxv(AT_EUID, geteuid());
	push_auxv(AT_EGID, getegid());

	push_auxv(AT_EXECFN, foo_str_g);
	push_auxv(AT_SECURE, false);
	push_auxv(AT_HWCAP, 0);
	push_auxv(AT_CLKTCK, sysconf(_SC_CLK_TCK));
	push_auxv(AT_RANDOM, auxv_salt_g);

	push_auxv(AT_NULL, 0);

	elf->stack_start = stk;
}

} // namespace dbt

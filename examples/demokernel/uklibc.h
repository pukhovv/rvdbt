#pragma once

#ifdef __riscv

// Compile with:
// -ffreestanding -march=rv32i -fpic -fpie -nostartfiles -nolibc -static

int do_syscall1(int no, int arg1)
{
	register int a0 __asm("a0") = no;
	register int a1 __asm("a1") = arg1;
	__asm volatile("ecall\n\t" : "=r"(a0) : "r"(a0), "r"(a1) : "memory");
	return a0;
}

int getnum()
{
	return do_syscall1(1, 0);
}

void putnum(int x)
{
	do_syscall1(2, x);
}

void doexit(int x)
{
	do_syscall1(3, x);
}

void __attribute__((naked)) _start()
{
	__asm__("la gp, __global_pointer$\n\t"
		"jal ra, main\n\t");
	doexit(0);
}

#else
#include "uklibc_host.h"
#endif

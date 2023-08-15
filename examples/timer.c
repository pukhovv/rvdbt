#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

struct timespec64 {
	int64_t tv_sec;
	int64_t tv_nsec;
};

static int clock_gettime64(clockid_t cid, struct timespec64 *ts)
{
	register int a7 __asm("a7") = 403;
	register int a0 __asm("a0") = cid;
	register void *a1 __asm("a1") = ts;
	__asm volatile("ecall\n\t" : "=r"(a0) : "r"(a0), "r"(a1), "r"(a7) : "memory");
	return a0;
}

int main()
{
	struct timeval tv;
	if (gettimeofday(&tv, NULL) != 0) {
		return 1;
	}

	struct timespec64 kt;
	if (clock_gettime64(0, &kt) != 0) {
		return 1;
	}

	printf("gettimeofday   : s=%lld us=%lld\n", (long long)tv.tv_sec, (long long)tv.tv_usec);
	printf("clock_gettime64: s=%lld ns=%lld\n", (long long)kt.tv_sec, (long long)kt.tv_nsec);
	return 0;
}

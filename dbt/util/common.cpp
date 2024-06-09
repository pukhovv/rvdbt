#include "dbt/util/common.h"
#include "dbt/util/allocator.h"
#include "dbt/util/fsmanager.h"
#include "dbt/util/logger.h"
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <vector>

#include <execinfo.h>
#include <fcntl.h>
#include <sys/file.h>

namespace dbt
{

LOG_STREAM(dbt);

static __attribute__((noreturn)) void dbtabort()
{
	std::array<void *, 8> fpptrs;
	int stack_sz = backtrace(fpptrs.data(), fpptrs.size());
	char **syms = backtrace_symbols(fpptrs.data(), stack_sz);

	if (syms != nullptr) {
		fprintf(stderr, "backtrace\n");
		for (int s = 0; s < stack_sz; s++) {
			fprintf(stderr, "\tat %s\n", syms[s]);
		}
		free(syms);
	}
	abort();
}

void __attribute__((noreturn)) Panic(char const *msg)
{
	fprintf(stderr, "Panic: %s\n", msg != nullptr ? msg : "???");
	dbtabort();
}

void __attribute__((noreturn)) Panic(std::string const &msg)
{
	fprintf(stderr, "Panic: %s\n", msg.c_str());
	dbtabort();
}

std::string MakeHexStr(uint8_t const *data, size_t sz)
{
	std::string hstr;
	hstr.reserve(sz * 2);
	char const *hexdig_str = "0123456789abcdef";

	for (size_t i = 0; i < sz; ++i) {
		hstr += hexdig_str[(data[i] >> 4) & 0xf];
		hstr += hexdig_str[data[i] & 0xf];
	}
	return hstr;
}

} // namespace dbt

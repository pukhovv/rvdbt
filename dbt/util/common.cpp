#include "dbt/util/common.h"
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace dbt
{

void __attribute__((noreturn)) Panic(char const *msg)
{
	fprintf(stderr, "Panic: %s\n", msg);
	abort();
}

void __attribute__((noreturn)) Panic(std::string const &msg)
{
	fprintf(stderr, "Panic: %s\n", msg.c_str());
	abort();
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

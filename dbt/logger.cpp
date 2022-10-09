#include "dbt/logger.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

namespace dbt
{

inline int xvsnprintf(char *dst, size_t n, char const *fmt, va_list args)
{
	int rc = vsnprintf(dst, n, fmt, args);
	assert(rc >= 0);
	return rc;
}

void LogStream::write(const char *str) const
{
	std::array<char, 2048> buf;
	auto cur = buf.begin();
	auto const end = buf.end() - 2;

	cur += snprintf(cur, end - cur, "%-12s%s", prefix, str);
	*cur++ = '\n';

	fwrite(buf.data(), cur - buf.begin(), sizeof(char), stderr);
}

void LogStream::operator()(const char *fmt, ...) const
{
	std::array<char, 2048> buf;
	auto cur = buf.begin();
	auto const end = buf.end() - 2;

	va_list args;
	va_start(args, fmt);

	cur += snprintf(cur, end - cur, "%-12s", prefix);
	cur += vsnprintf(cur, end - cur, fmt, args);
	*cur++ = '\n';
	va_end(args);

	fwrite(buf.data(), cur - buf.begin(), sizeof(char), stderr);
}

} // namespace dbt

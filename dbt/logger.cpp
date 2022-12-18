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

void LogStream::commit_write(const char *str) const
{
	std::array<char, 4096 * 2> buf;
	auto cur = buf.begin();
	auto const end = buf.end() - 2;

	int res = snprintf(cur, end - cur, "%-12s%s", prefix, str);
	assert(res >= 0);
	cur += res;

	*cur++ = '\n';

	fwrite(buf.data(), cur - buf.begin(), sizeof(char), stderr);
}

void LogStream::commit_printf(const char *fmt, ...) const
{
	std::array<char, 2048> buf;
	auto cur = buf.begin();
	auto const end = buf.end() - 2;

	va_list args;
	va_start(args, fmt);

	int res = snprintf(cur, end - cur, "%-12s", prefix);
	assert(res >= 0);
	cur += res;

	assert(strlen(fmt) < end - cur); // use write instead
	res = vsnprintf(cur, end - cur, fmt, args);
	assert(res >= 0);
	cur += res;

	*cur++ = '\n';
	va_end(args);

	fwrite(buf.data(), cur - buf.begin(), sizeof(char), stderr);
}

LogStream::Setup::Setup(LogStream &s)
{
	Logger::setup(s);
}

Logger *Logger::get()
{
	static Logger g_logger{};
	return &g_logger;
}

void Logger::setup(LogStream &s)
{
	auto self = get();
	self->streams.emplace(s.name, &s);
}

void Logger::enable(char const *name)
{
	auto self = get();
	auto s = self->streams.find(name);
	if (s != self->streams.end()) {
		s->second->level = true;
	}
}

} // namespace dbt

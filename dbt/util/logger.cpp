#include "dbt/util/logger.h"
#include <array>
#include <cstdarg>
#include <cstdio>
#include <vector>

namespace dbt
{

void __attribute__((noreturn)) Panic(char const *msg)
{
	fprintf(stderr, "Panic: %s\n", msg);
	abort();
}

inline int xvsnprintf(char *dst, size_t n, char const *fmt, va_list args)
{
	int rc = vsnprintf(dst, n, fmt, args);
	assert(rc >= 0);
	return rc;
}

static thread_local std::vector<char> g_log_buf(1024 * 2);

void LogStream::commit_write(const char *str) const
{
	auto &buf = g_log_buf;

retry:
	int res = snprintf(buf.data(), buf.size(), "%-12s%s\n", prefix, str);
	assert(res > 0);
	if (res >= buf.size()) {
		buf.resize(res + 1);
		goto retry;
	}

	fwrite(buf.data(), res, sizeof(char), stderr);
}

void LogStream::commit_printf(const char *fmt, ...) const
{
	auto &buf = g_log_buf;
	size_t cur = 0;

	int res = snprintf(buf.data(), buf.size(), "%-12s", prefix);
	assert(res >= 0);
	cur += res;

	va_list args;
	va_start(args, fmt);
retry:
	res = vsnprintf(buf.data() + cur, buf.size() - cur, fmt, args);
	assert(res >= 0);
	if (res + cur + 1 >= buf.size()) { // +1 for newline
		buf.resize(res + cur + 1 + 1);
		goto retry;
	}
	cur += res;

	va_end(args);

	buf[cur] = '\n';
	cur += 1;
	assert(cur <= buf.size());

	fwrite(buf.data(), cur, sizeof(char), stderr);
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

#include "dbt/util/logger.h"
#include "dbt/util/allocator.h"
#include <array>
#include <cstdarg>
#include <cstdio>
#include <vector>

namespace dbt
{
inline int xvsnprintf(char *dst, size_t n, char const *fmt, va_list args)
{
	int rc = vsnprintf(dst, n, fmt, args);
	assert(rc >= 0);
	return rc;
}

static thread_local std::vector<char> g_log_buf(1024 * 2);

void LogStreamI::commit_write(const char *str) const
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

void LogStreamI::commit_printf(const char *fmt, ...) const
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

LogStreamI::Setup::Setup(LogStreamI &s)
{
	Logger::setup(s);
}

Logger *Logger::get()
{
	static Logger g_logger{};
	return &g_logger;
}

void Logger::setup(LogStreamI &s)
{
	auto self = get();
	if (s.initialized.load()) {
		return;
	}

	std::lock_guard lk(self->mtx);
	if (s.initialized.load()) {
		return;
	}
	self->streams.emplace(s.name, &s);
	s.initialized.store(true);
}

void Logger::enable(char const *name, char const *path)
{
	auto self = get();
	std::lock_guard lk(self->mtx);

	auto s = self->streams.find(name);
	if (s == self->streams.end()) {
		fprintf(stderr, "nonexisting log stream: %s\n", name);
		return;
	}

	s->second->level = true;
	if (path != nullptr) {
		unreachable(""); // TODO: redirect
	}
}

} // namespace dbt

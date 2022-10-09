#pragma once

namespace dbt
{

struct LogStream {
	consteval LogStream(char const *prefix_) : prefix(prefix_) {}

	void write(char const *str) const;
	void operator()(char const *fmt, ...) const;

	static constexpr bool enabled()
	{
		return true;
	}

private:
	char const *prefix;
};

struct LogStreamNull {
	consteval LogStreamNull(char const *prefix_) {}

	inline void write(char const *str) const {}
	inline void operator()(char const *fmt, ...) const {}

	static constexpr bool enabled()
	{
		return false;
	}
};

#ifndef NDEBUG
#define LOG_STREAM(name, pref) constinit inline LogStream name(pref)
#else
#define LOG_STREAM(name, pref) constinit inline LogStreamNull name(pref)
#endif

} // namespace dbt

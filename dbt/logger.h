#pragma once

#include "dbt/common.h"
#include <map>
#include <mutex>

namespace dbt
{

struct LogStream {
	consteval LogStream(char const *name_, char const *prefix_) : name(name_), prefix(prefix_) {}

	ALWAYS_INLINE void write(char const *str) const
	{
		if (unlikely(enabled())) {
			commit_write(str);
		}
	}

	template <typename... Args>
	ALWAYS_INLINE void operator()(char const *fmt, const Args... args) const
	{
		if (unlikely(enabled())) {
			commit_printf(fmt, args...);
		}
	}

	ALWAYS_INLINE bool enabled() const
	{
		return level;
	}

	struct Setup {
		Setup(LogStream &s);
	};

private:
	friend struct Logger;

	void commit_write(char const *str) const;
	void commit_printf(char const *fmt, ...) const;

	char const *name{};
	char const *prefix{};
	bool level{};
};

struct LogStreamNull {
	consteval LogStreamNull() {}

	ALWAYS_INLINE void write(char const *str) const {}

	template <typename... Args>
	ALWAYS_INLINE void operator()(char const *fmt, const Args... args) const
	{
	}

	static constexpr bool enabled()
	{
		return false;
	}
};

struct Logger {
	static void enable(char const *name);

private:
	friend struct LogStream::Setup;

	Logger() = default;
	static Logger *get();

	static void setup(LogStream &s);

	std::map<std::string, LogStream *> streams{};
};

#ifndef NDEBUG
#define LOG_STREAM(name)                                                                                     \
	constinit inline LogStream log_##name{#name, "[" #name "]"};                                         \
	namespace                                                                                            \
	{                                                                                                    \
	inline ::dbt::LogStream::Setup setup_log_##name{log_##name};                                         \
	}
#else
#define LOG_STREAM(name) constinit inline LogStreamNull log_##name{};
#endif

} // namespace dbt

#pragma once

#include "dbt/util/common.h"
#include <atomic>
#include <map>
#include <mutex>

namespace dbt
{

struct LogStreamI {
	consteval LogStreamI(char const *name_, char const *prefix_) : name(name_), prefix(prefix_) {}

	ALWAYS_INLINE void write(char const *str) const
	{
		if (unlikely(enabled())) {
			commit_write(str);
		}
	}

	ALWAYS_INLINE void operator()(char const *fmt) const
	{
		write(fmt);
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
		Setup(LogStreamI &s);
	};

private:
	friend struct Logger;

	void commit_write(char const *str) const;
	void commit_printf(char const *fmt, ...) const;

	std::atomic<bool> initialized{};

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
	static void enable(char const *name, char const *path = nullptr);

private:
	friend struct LogStreamI::Setup;

	Logger() = default;
	static Logger *get();

	static void setup(LogStreamI &s);

	std::mutex mtx{};
	std::map<std::string, LogStreamI *> streams{};
};

#ifndef NDEBUG
using LogStream = LogStreamI;
#define LOG_STREAM(name)                                                                                     \
	constinit inline LogStream log_##name{#name, "[" #name "]"};                                         \
	namespace                                                                                            \
	{                                                                                                    \
	inline ::dbt::LogStream::Setup setup_log_##name{log_##name};                                         \
	}
#else
using LogStream = LogStreamNull;
#define LOG_STREAM(name) constinit inline LogStreamNull log_##name{};
#endif

} // namespace dbt

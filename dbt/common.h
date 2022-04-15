#pragma once

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <type_traits>

#include "dbt/config.h"

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

typedef int64_t i64;
typedef int32_t i32;
typedef int16_t i16;
typedef int8_t i8;

#define likely(v) __builtin_expect(!!(v), 1)
#define unlikely(v) __builtin_expect(!!(v), 0)

#define ALWAYS_INLINE inline __attribute__((always_inline))
#define NOINLINE __attribute__((noinline))

#ifndef __clang__
#error "This project requires __attribute__((musttail)) which is supported only in Clang"
#define MUSTTAIL
#else
#define MUSTTAIL __attribute__((musttail))
#endif

#define unreachable(str)                                                                                     \
	do {                                                                                                 \
		assert((str) && 0);                                                                          \
		__builtin_unreachable();                                                                     \
	} while (0)

template <typename T, typename A>
inline T roundup(T x, A y)
{
	return ((x + (y - 1)) / y) * y;
}

template <typename T, typename A>
inline T rounddown(T x, A y)
{
	return x - x % y;
}

template <typename T>
T unaligned_load(T const *ptr)
{
	struct uT {
		T x;
	} __attribute__((packed));
	return reinterpret_cast<uT const *>(ptr)->x;
}

template <typename T>
void unaligned_store(T *ptr, T val)
{
	struct uT {
		T x;
	} __attribute__((packed));
	reinterpret_cast<uT *>(ptr)->x = val;
}

template <typename T, typename P>
typename std::enable_if_t<!std::is_same_v<T, P>, T> unaligned_load(P const *ptr)
{
	return unaligned_load<T>(reinterpret_cast<T const *>(ptr));
}

template <typename T, typename P, typename V>
typename std::enable_if_t<!std::is_same_v<T, P>> unaligned_store(P *ptr, V val)
{
	unaligned_store<T>(reinterpret_cast<T *>(ptr), static_cast<T>(val));
}

struct Logger {
	static constexpr bool null = false;
	template <typename T>
	inline void write(T &&x)
	{
		std::cerr << x;
	}
	Logger() = default;
	Logger(char const *pref)
	{
		write(pref);
	}
	~Logger()
	{
		write("\n");
		std::cerr.flags(flags);
	}
	template <typename T>
	Logger &operator<<(T &&x)
	{
		write(std::forward<T>(x));
		return *this;
	}

private:
	std::ios_base::fmtflags flags{std::cerr.flags()};
};

struct NullLogger {
	static constexpr bool null = true;
	template <typename T>
	inline void write(T &&x)
	{
	}
	template <typename T>
	NullLogger operator<<(T &&x)
	{
		return NullLogger();
	}
};

// #define LOG_TRACE_ENABLE

#if defined(LOG_TRACE_ENABLE) && !defined(NDEBUG)
#define log_trace() Logger("[TRACE]: ")
#else
#define log_trace() NullLogger()
#endif
#ifndef NDEBUG
#define log_bt() Logger("[BT]: ")
#define log_cflow() Logger("[CFLOW]: ")
#define log_ukernel() Logger("[UKERNEL]: ")
#else
#define log_bt() NullLogger()
#define log_cflow() NullLogger()
#define log_ukernel() NullLogger()
#endif

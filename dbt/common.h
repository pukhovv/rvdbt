#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "dbt/config.h"

typedef uintptr_t uptr;
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;

typedef intptr_t iptr;
typedef int64_t i64;
typedef int32_t i32;
typedef int16_t i16;
typedef int8_t i8;

#define POISON_PTR ((void *)0xb00bab00deaddead)
#define POISON_GUEST ((u32)0xdedb00ba)

#define likely(v) __builtin_expect(!!(v), 1)
#define unlikely(v) __builtin_expect(!!(v), 0)

#define ALWAYS_INLINE inline __attribute__((always_inline))
#define NOINLINE __attribute__((noinline))

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

namespace dbt
{
void __attribute__((noreturn)) Panic(char const *msg = "");
} // namespace dbt

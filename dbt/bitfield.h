#pragma once

#include "dbt/common.h"

template <typename T, u8 l, u8 h>
struct bf_range {
	static_assert(h >= l);
	static constexpr u8 size = h - l + 1;
	static_assert(sizeof(T) * 8 >= size);

	template <typename C>
	requires std::is_unsigned_v<T>
	static inline constexpr T decode(C c)
	{
		return extract_bits(static_cast<std::make_unsigned_t<C>>(c));
	}

	template <typename C>
	requires std::is_signed_v<T>
	static inline constexpr T decode(C c)
	{
		return extract_bits(static_cast<std::make_signed_t<C>>(c));
	}

private:
	template <typename C>
	static inline constexpr T extract_bits(C c)
	{
		constexpr u8 u_msb = sizeof(C) * 8 - 1;
		return c << (u_msb - h) >> (u_msb - h + l);
	}
};

template <u8 l_, u8 h_>
struct bf_pt {
	static constexpr auto l = l_;
	static constexpr auto h = h_;
};

template <typename T, typename P, typename... Args>
struct bf_seq {
	template <typename C>
	static inline constexpr T decode(C c)
	{
		return B::decode(c) | (bf_seq<T, Args...>::decode(c) << B::size);
	}

private:
	using B = bf_range<std::make_unsigned_t<T>, P::l, P::h>;
};

template <typename T, typename P>
struct bf_seq<T, P> {
	template <typename C>
	static inline constexpr T decode(C c)
	{
		return bf_range<T, P::l, P::h>::decode(c);
	}
};

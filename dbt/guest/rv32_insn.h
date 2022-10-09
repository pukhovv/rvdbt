#pragma once

#include "dbt/common.h"
#include "dbt/guest/rv32_ops.h"
#include <limits>
#include <ostream>
#include <utility>

namespace dbt::rv32::insn
{

namespace Flags
{
enum Types : u32 {
	None = 0,
	Branch = 1 << 1u,
	MayTrap = 1 << 2u,
	HasRd = 1 << 3u,
};
}

struct Base {
	u8 opcode()
	{
		return bitseq(bitrange{0, 6});
	}

	u32 raw;

protected:
	struct bitrange {
		u8 l, h;
	};

	inline constexpr std::pair<u32, u8> read_bitrange(bitrange r)
	{
		u32 sh = r.h - r.l + 1;
		u32 f = (raw >> r.l) & ((u32(1) << sh) - 1);
		return std::make_pair(f, sh);
	}

	inline constexpr u32 bitseq(bitrange r)
	{
		return read_bitrange(r).first;
	}

	template <typename... Types>
	inline constexpr u32 bitseq(bitrange r, Types... args)
	{
		auto v = read_bitrange(r);
		return v.first | (bitseq(args...) << v.second);
	}

	inline u32 bitseq(u8 se)
	{
		return se ? std::numeric_limits<u32>::max() : 0;
	}

	inline u8 rd()
	{
		return bitseq(bitrange{7, 11});
	}
	inline u8 rs1()
	{
		return bitseq(bitrange{15, 19});
	}
	inline u8 rs2()
	{
		return bitseq(bitrange{20, 24});
	}
	inline u8 funct3()
	{
		return bitseq(bitrange{12, 14});
	}
	inline u8 funct7()
	{
		return bitseq(bitrange{25, 31});
	}
	inline u16 funct12()
	{
		return bitseq(bitrange{20, 31});
	}
	inline u8 imm_se()
	{
		return bitseq(bitrange{31, 31});
	}
	static constexpr Flags::Types gen_flags = Flags::None;
};

#define BASE_FIELD(name)                                                                                     \
	auto name()                                                                                          \
	{                                                                                                    \
		return Base::name();                                                                         \
	}

struct R : public Base {
	BASE_FIELD(rd);
	BASE_FIELD(rs1);
	BASE_FIELD(rs2);

protected:
	static constexpr Flags::Types gen_flags = Flags::HasRd;
};

struct I : public Base {
	BASE_FIELD(rd);
	BASE_FIELD(rs1);
	inline i16 imm()
	{
		return bitseq(bitrange{20, 30}, imm_se());
	}

protected:
	static constexpr Flags::Types gen_flags = Flags::HasRd;
};

struct IS : public Base { // imm shifts
	BASE_FIELD(rd);
	BASE_FIELD(rs1);
	inline i16 imm()
	{
		return bitseq(bitrange{20, 24});
	}

protected:
	static constexpr Flags::Types gen_flags = Flags::HasRd;
};

struct S : public Base {
	BASE_FIELD(rs1);
	BASE_FIELD(rs2);
	inline i16 imm()
	{
		return bitseq(bitrange{7, 11}, bitrange{25, 30}, imm_se());
	}

protected:
	static constexpr Flags::Types gen_flags = Flags::None;
};

struct B : public Base {
	BASE_FIELD(rs1);
	BASE_FIELD(rs2);
	inline i16 imm()
	{
		return bitseq(bitrange{8, 11}, bitrange{25, 30}, bitrange{7, 7}, imm_se()) << 1u;
	}

protected:
	static constexpr Flags::Types gen_flags = Flags::None;
};

struct U : public Base {
	BASE_FIELD(rd);
	inline i32 imm()
	{
		return bitseq(bitrange{12, 31}) << 12u;
	}

protected:
	static constexpr Flags::Types gen_flags = Flags::HasRd;
};

struct J : public Base {
	BASE_FIELD(rd);
	inline i32 imm()
	{
		return bitseq(bitrange{21, 30}, bitrange{20, 20}, bitrange{12, 19}, imm_se()) << 1u;
	}

protected:
	static constexpr Flags::Types gen_flags = Flags::HasRd;
};

char const *GRPToName(u8 r);
std::ostream &operator<<(std::ostream &o, Base i);
std::ostream &operator<<(std::ostream &o, R i);
std::ostream &operator<<(std::ostream &o, I i);
std::ostream &operator<<(std::ostream &o, IS i);
std::ostream &operator<<(std::ostream &o, S i);
std::ostream &operator<<(std::ostream &o, B i);
std::ostream &operator<<(std::ostream &o, U i);
std::ostream &operator<<(std::ostream &o, J i);

#define OP(name, format_, flags_)                                                                            \
	struct Insn_##name : format_ {                                                                       \
		using format = format_;                                                                      \
		static constexpr const char *opcode_str = #name;                                             \
		static constexpr std::underlying_type_t<Flags::Types> flags = (flags_) | gen_flags;          \
	};
RV32_OPCODE_LIST()
#undef OP

enum class Op : u8 {
#define OP(name, format_, flags_) _##name,
	RV32_OPCODE_LIST() OP(last, _, _)
#undef OP
};

} // namespace dbt::rv32::insn

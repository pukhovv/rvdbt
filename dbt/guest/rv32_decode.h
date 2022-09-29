#pragma once

#include "dbt/common.h"
#include "dbt/guest/rv32_ops.h"
#include <limits>
#include <ostream>
#include <utility>

namespace dbt::rv32
{

namespace insn
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

struct DecodeParams : public Base {
	BASE_FIELD(funct3);
	BASE_FIELD(funct7);
	BASE_FIELD(funct12);
};

#define OP(name, format_, flags_)                                                                            \
	struct Insn_##name : format_ {                                                                       \
		using format = format_;                                                                      \
		static constexpr const char *opcode_str = #name;                                             \
		static constexpr std::underlying_type_t<Flags::Types> flags = (flags_) | gen_flags;          \
	};
RV32_OPCODE_LIST()
#undef OP

} // namespace insn

#define RV32_DECODE_SWITCH(__inst)                                                                          \
	{                                                                                                    \
		switch (__inst.opcode()) {                                                                   \
		case 0b0110111:                                                                              \
			OP(lui);                                                                             \
		case 0b0010111:                                                                              \
			OP(auipc);                                                                           \
		case 0b1101111:                                                                              \
			OP(jal);                                                                             \
		case 0b1100111:                                                                              \
			switch (__inst.funct3()) {                                                           \
			case 0b000:                                                                          \
				OP(jalr);                                                                    \
			default:                                                                             \
				OP_ILL                                                                       \
			}                                                                                    \
		case 0b1100011:                                                                              \
			switch (__inst.funct3()) {                                                           \
			case 0b000:                                                                          \
				OP(beq);                                                                     \
			case 0b001:                                                                          \
				OP(bne);                                                                     \
			case 0b100:                                                                          \
				OP(blt);                                                                     \
			case 0b101:                                                                          \
				OP(bge);                                                                     \
			case 0b110:                                                                          \
				OP(bltu);                                                                    \
			case 0b111:                                                                          \
				OP(bgeu);                                                                    \
			default:                                                                             \
				OP_ILL                                                                       \
			}                                                                                    \
		case 0b0000011: /* lX */                                                                     \
			switch (__inst.funct3()) {                                                           \
			case 0b000:                                                                          \
				OP(lb);                                                                      \
			case 0b001:                                                                          \
				OP(lh);                                                                      \
			case 0b010:                                                                          \
				OP(lw);                                                                      \
			case 0b100:                                                                          \
				OP(lbu);                                                                     \
			case 0b101:                                                                          \
				OP(lhu);                                                                     \
			default:                                                                             \
				OP_ILL                                                                       \
			}                                                                                    \
		case 0b0100011: /* sX */                                                                     \
			switch (__inst.funct3()) {                                                           \
			case 0b000:                                                                          \
				OP(sb);                                                                      \
			case 0b001:                                                                          \
				OP(sh);                                                                      \
			case 0b010:                                                                          \
				OP(sw);                                                                      \
			default:                                                                             \
				OP_ILL                                                                       \
			}                                                                                    \
		case 0b0010011: /* i-type arithm */                                                          \
			switch (__inst.funct3()) {                                                           \
			case 0b000:                                                                          \
				OP(addi);                                                                    \
			case 0b010:                                                                          \
				OP(slti);                                                                    \
			case 0b011:                                                                          \
				OP(sltiu);                                                                   \
			case 0b100:                                                                          \
				OP(xori);                                                                    \
			case 0b110:                                                                          \
				OP(ori);                                                                     \
			case 0b111:                                                                          \
				OP(andi);                                                                    \
			case 0b001:                                                                          \
				OP(slli);                                                                    \
			case 0b101:                                                                          \
				switch (__inst.funct7()) {                                                   \
				case 0b0000000:                                                              \
					OP(srli);                                                            \
				case 0b0100000:                                                              \
					OP(srai);                                                            \
				default:                                                                     \
					OP_ILL                                                               \
				}                                                                            \
			default:                                                                             \
				OP_ILL                                                                       \
			}                                                                                    \
		case 0b0110011: /* r-type arithm */                                                          \
			switch (__inst.funct3()) {                                                           \
			case 0b000:                                                                          \
				switch (__inst.funct7()) {                                                   \
				case 0b0000000:                                                              \
					OP(add);                                                             \
				case 0b0100000:                                                              \
					OP(sub);                                                             \
				default:                                                                     \
					OP_ILL                                                               \
				}                                                                            \
			case 0b001:                                                                          \
				OP(sll);                                                                     \
			case 0b010:                                                                          \
				OP(slt);                                                                     \
			case 0b011:                                                                          \
				OP(sltu);                                                                    \
			case 0b100:                                                                          \
				OP(xor);                                                                     \
			case 0b101:                                                                          \
				switch (__inst.funct7()) {                                                   \
				case 0b0000000:                                                              \
					OP(srl);                                                             \
				case 0b0100000:                                                              \
					OP(sra);                                                             \
				default:                                                                     \
					OP_ILL                                                               \
				}                                                                            \
			case 0b110:                                                                          \
				OP(or);                                                                      \
			case 0b111:                                                                          \
				OP(and);                                                                     \
			default:                                                                             \
				OP_ILL                                                                       \
			}                                                                                    \
		case 0b0001111:                                                                              \
			OP_ILL /* fence */                                                                   \
			    case 0b1110011 : switch (__inst.funct3())                                        \
			{                                                                                    \
			case 0b000:                                                                          \
				switch (__inst.funct12()) {                                                  \
				case 0b000000000000:                                                         \
					OP(ecall);                                                           \
				case 0b000000000001:                                                         \
					OP(ebreak);                                                          \
				default:                                                                     \
					OP_ILL;                                                              \
				}                                                                            \
			default:                                                                             \
				OP_ILL /* csr* */                                                            \
			}                                                                                    \
		default:                                                                                     \
			OP_ILL                                                                               \
		}                                                                                            \
	}

} // namespace dbt::rv32

#pragma once

#include "dbt/guest/rv32_insn.h"

namespace dbt::rv32::insn
{

template <typename Provider>
struct Decoder {
	using DType = decltype(Provider::_ill);

	static DType Decode(void *insn)
	{
		auto in = *reinterpret_cast<DecodeParams *>(insn);
#define OP(name) return Provider::_##name;
#define OP_ILL OP(ill)

		switch (in.opcode()) {
		case 0b0110111:
			OP(lui);
		case 0b0010111:
			OP(auipc);
		case 0b1101111:
			OP(jal);
		case 0b1100111:
			switch (in.funct3()) {
			case 0b000:
				OP(jalr);
			default:
				OP_ILL;
			}
		case 0b1100011: /* bcc */
			switch (in.funct3()) {
			case 0b000:
				OP(beq);
			case 0b001:
				OP(bne);
			case 0b100:
				OP(blt);
			case 0b101:
				OP(bge);
			case 0b110:
				OP(bltu);
			case 0b111:
				OP(bgeu);
			default:
				OP_ILL;
			}
		case 0b0000011: /* lX */
			switch (in.funct3()) {
			case 0b000:
				OP(lb);
			case 0b001:
				OP(lh);
			case 0b010:
				OP(lw);
			case 0b100:
				OP(lbu);
			case 0b101:
				OP(lhu);
			default:
				OP_ILL;
			}
		case 0b0100011: /* sX */
			switch (in.funct3()) {
			case 0b000:
				OP(sb);
			case 0b001:
				OP(sh);
			case 0b010:
				OP(sw);
			default:
				OP_ILL;
			}
		case 0b0010011: /* i-type arithm */
			switch (in.funct3()) {
			case 0b000:
				OP(addi);
			case 0b010:
				OP(slti);
			case 0b011:
				OP(sltiu);
			case 0b100:
				OP(xori);
			case 0b110:
				OP(ori);
			case 0b111:
				OP(andi);
			case 0b001:
				OP(slli);
			case 0b101:
				switch (in.funct7()) {
				case 0b0000000:
					OP(srli);
				case 0b0100000:
					OP(srai);
				default:
					OP_ILL;
				}
			default:
				OP_ILL;
			}
		case 0b0110011: /* r-type arithm */
			switch (in.funct3()) {
			case 0b000:
				switch (in.funct7()) {
				case 0b0000000:
					OP(add);
				case 0b0100000:
					OP(sub);
				default:
					OP_ILL;
				}
			case 0b001:
				OP(sll);
			case 0b010:
				OP(slt);
			case 0b011:
				OP(sltu);
			case 0b100:
				OP(xor);
			case 0b101:
				switch (in.funct7()) {
				case 0b0000000:
					OP(srl);
				case 0b0100000:
					OP(sra);
				default:
					OP_ILL;
				}
			case 0b110:
				OP(or);
			case 0b111:
				OP(and);
			default:
				OP_ILL;
			}
		case 0b0001111:
			OP_ILL; /* fence */
		case 0b1110011:
			switch (in.funct3()) {
			case 0b000:
				switch (in.funct12()) {
				case 0b000000000000:
					OP(ecall);
				case 0b000000000001:
					OP(ebreak);
				default:
					OP_ILL;
				}
			default:
				OP_ILL; /* csr* */
			}
		default:
			OP_ILL;
		}
#undef OP
#undef OP_ILL
	}

private:
	Decoder() = delete;
	struct DecodeParams : public Base {
		INSN_FIELD(funct3);
		INSN_FIELD(funct7);
		INSN_FIELD(funct12);
	};
};

} // namespace dbt::rv32::insn

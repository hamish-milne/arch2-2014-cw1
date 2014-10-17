/**
 * MIPS-I CPU Implementation
 * (C) Hamish Milne 2014
 *
 * Implements all of the specified MIPS-I instructions,
 * excluding SYSCALL, COPz, LWCz and SWCz
 **/

#include "mips_cpu.h"
#include "stdio.h"
#include "limits.h"
#include "stdbool.h"

#define NUM_REGS 32

#define BLANK 0,0,0,0

/// Two words next to each other, the high and low parts
typedef struct
{
	uint32_t lo, hi;
} s_hi_lo;

/// A double word register, accessible in full or in parts
typedef union
{
	uint64_t full;
	s_hi_lo parts;
} long_reg;

/// CPU state structure
struct mips_cpu_impl
{
	/// Pointer to memory object
	mips_mem_h mem;
	/// Debug level
	unsigned debug;
	/// Output for debug messages
	FILE* output;
	/// Program counter
	uint32_t pc;
	/// The $HI and $LO registers
	long_reg hi_lo;
	/// General purpose registers
	uint32_t reg[NUM_REGS];
};

/// Data for an R-type instruction
typedef struct
{
	unsigned opcode, s1, s2, d, shift, f;
} rtype;

/// Data for an I-type instruction
typedef struct
{
	unsigned opcode, s, d, imm;
} itype;

/// Data for a J-type instruction
typedef struct
{
	unsigned opcode, imm;
} jtype;

/// Signature for a general operation
typedef mips_error (*op)(mips_cpu_h state, uint32_t instruction);

/// Signature for an R-type operation
typedef mips_error (*rtype_op)(mips_cpu_h state, rtype operands);

rtype get_rtype(uint32_t instr)
{
	rtype ret;
	unsigned* val = (unsigned*)&ret;
	*val++ = instr >> 26;
	*val++ = (instr >> 21) & 0x1F;
	*val++ = (instr >> 16) & 0x1F;
	*val++ = (instr >> 11) & 0x1F;
	*val++ = (instr >> 6) & 0x1F;
	*val++ = instr & 0x3F;
	return ret;
}

itype get_itype(uint32_t instr)
{
	itype ret;
	unsigned* val = (unsigned*)&ret;
	*val++ = instr >> 26;
	*val++ = (instr >> 21) & 0x1F;
	*val++ = (instr >> 16) & 0x1F;
	*val++ = instr & 0xFFFF;
	return ret;
}

jtype get_jtype(uint32_t instr)
{
	jtype ret;
	ret.opcode = instr >> 26;
	ret.imm = instr & 0x3FFFFFF;
	return ret;
}

/// Sets a register, ensuring that $0 == 0 and outputting debug information
void set_reg(mips_cpu_h state, unsigned index, uint32_t value)
{
	state->reg[index] = index ? value : 0;
	if(state->debug > 1 && state->output != NULL)
		fprintf(state->output, "$%d = %d (%a)", index, (int32_t)value, value);
}

/// Dummy function for non-implemented instructions
mips_error not_impl(mips_cpu_h state, uint32_t instruction)
{
	return mips_ErrorNotImplemented;
}

mips_error not_impl_r(mips_cpu_h state, rtype operands)
{
	return mips_ErrorNotImplemented;
}

/// Jump
mips_error jump(mips_cpu_h state, uint32_t instruction)
{
	jtype operands = get_jtype(instruction);
	state->pc = (state->pc & 0xF0000000) | ((int32_t)operands.imm << 2);
	return mips_Success;
}

/// Jump and link
mips_error jal(mips_cpu_h state, uint32_t instruction)
{
	jtype operands = get_jtype(instruction);
	set_reg(state, 31, state->pc + 4);
	state->pc = (state->pc & 0xF0000000) | ((int32_t)operands.imm << 2);
	return mips_Success;
}

/// General function for all instructions that
/// branch on condition, comparing to zero
mips_error branch_zero(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	int32_t value = (int32_t)state->reg[operands.s];
	bool result;
	/// Combine the opcode and the final bit of the 'd' field
	/// Use to determine exact instruction
	switch((operands.opcode << 1) + (operands.d & 1))
	{
	case 2: /// BLZ
		result = value < 0;
		break;
	case 3: /// BGEZ
		result = value >= 0;
		break;
	case 12: /// BLEZ
		result = value <= 0;
		break;
	case 14: /// BGZ
		result = value > 0;
		break;
	default:
		return mips_ExceptionInvalidInstruction;
	}
	if(result)
	{
		/// Use the first bit of the 'd' field
		/// to determine if we need to link
		if(operands.d & 0x10)
			set_reg(state, 31, state->pc + 4);
		state->pc += (int32_t)operands.imm << 2;
	}
	else
	{
		state->pc += 4;
	}
	return mips_Success;
}

/// General instruction for conditional branch,
/// comparing two registers
mips_error branch_var(mips_cpu_h state, uint32_t instruction)
{
	uint32_t* regs = state->reg;
	itype operands = get_itype(instruction);
	bool result = regs[operands.s] == regs[operands.d];
	/// If the final bit of opcode is set, it's BNE
	/// otherwise BEQ
	if(operands.opcode & 1)
		result = !result;
	state->pc += result ? (int32_t)operands.imm << 2 : 4;
	return mips_Success;
}

/// Add immediate
mips_error addi(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	uint32_t value = state->reg[operands.s];
	uint32_t result;
	if(!(operands.opcode & 1))
	{
		int32_t x = (int32_t)value;
		int32_t y = (int32_t)operands.imm;
		if ((y > 0 && x > INT_MAX - y) ||
			(y < 0 && x < INT_MIN - y))
			return mips_ExceptionArithmeticOverflow;
		result = x + y;
	}
	else
	{
		result = value + operands.imm;
	}
	set_reg(state, operands.d, result);
	state->pc += 4;
	return mips_Success;
}

/// Set if less than
mips_error slti(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	uint32_t value = state->reg[operands.s];
	bool result;
	if(operands.opcode & 1)
		result = value < operands.imm;
	else
		result = (int32_t)value < (int32_t)operands.imm;
	set_reg(state, operands.d, (uint32_t)result);
	state->pc += 4;
	return mips_Success;
}

mips_error bitwise_imm(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	uint32_t value = state->reg[operands.s];
	uint32_t result;
	switch(operands.opcode & 3)
	{
	case 0:
		result = value & operands.imm;
		break;
	case 1:
		result = value | operands.imm;
		break;
	case 2:
		result = value ^ operands.imm;
		break;
	default:
		return mips_ExceptionInvalidInstruction;
	}
	set_reg(state, operands.d, result);
	state->pc += 4;
	return mips_Success;
}

mips_error lui(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	set_reg(state, operands.d, operands.imm << 16);
	state->pc += 4;
	return mips_Success;
}

mips_error mem_base(mips_cpu_h state, itype operands, bool load, int length, uint8_t* word)
{
	if(state->mem == NULL)
		return mips_ErrorInvalidHandle;
	mips_error error;
	uint32_t addr = state->reg[operands.s] + (int16_t)operands.imm;
	if(load)
		error = mips_mem_read(state->mem, addr, length, word);
	else
		error = mips_mem_write(state->mem, addr, length, word);
	return error;
}

mips_error lb(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	int8_t word;
	mips_error error = mem_base(state, operands, true, 1, (uint8_t*)&word);
	if(error)
		return error;
	set_reg(state, operands.d, (operands.opcode & 4) ? (uint32_t)word : (int32_t)word);
	state->pc += 4;
	return mips_Success;
}

mips_error lh(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	int16_t word;
	mips_error error = mem_base(state, operands, true, 1, (uint8_t*)&word);
	if(error)
		return error;
	set_reg(state, operands.d, (operands.opcode & 4) ? (uint32_t)word : (int32_t)word);
	state->pc += 4;
	return mips_Success;
}

mips_error lw(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	uint32_t word;
	mips_error error = mem_base(state, operands, true, 4, (uint8_t*)&word);
	if(error)
		return error;
	set_reg(state, operands.d, word);
	state->pc += 4;
	return mips_Success;
}

mips_error lwl(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	uint16_t word;
	mips_error error = mem_base(state, operands, true, 1, (uint8_t*)&word);
	if(error)
		return error;
	set_reg(state, operands.d, ((uint32_t)word << 16) | (state->reg[operands.d] & 0xFFFF));
	state->pc += 4;
	return mips_Success;
}

mips_error lwr(mips_cpu_h state, uint32_t instruction)
{
	if(state->mem == NULL)
		return mips_ErrorInvalidHandle;
	itype operands = get_itype(instruction);
	uint16_t word;
	uint32_t addr = state->reg[operands.s] + (int16_t)operands.imm - 1;
	mips_error error = mips_mem_read(state->mem, addr, 2, (uint8_t*)&word);
	if(error)
		return error;
	set_reg(state, operands.d, (uint32_t)word | (state->reg[operands.d] & 0xFFFF0000));
	state->pc += 4;
	return mips_Success;
}

mips_error sb(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	uint8_t word = (uint8_t)state->reg[operands.d];
	mips_error error = mem_base(state, operands, false, 1, &word);
	if(error)
		return error;
	state->pc += 4;
	return mips_Success;
}

mips_error sh(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	uint16_t word = (uint8_t)state->reg[operands.d];
	mips_error error = mem_base(state, operands, false, 1, (uint8_t*)&word);
	if(error)
		return error;
	state->pc += 4;
	return mips_Success;
}

mips_error sw(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	uint32_t word = state->reg[operands.d];
	mips_error error = mem_base(state, operands, false, 4, (uint8_t*)&word);
	if(error)
		return error;
	state->pc += 4;
	return mips_Success;
}

mips_error shift_base(mips_cpu_h state, rtype operands, uint32_t shift)
{
	uint32_t* regs = state->reg;
	uint32_t result;
	uint32_t value = regs[operands.s2];
	switch(operands.f & 3)
	{
	case 0:
		result = value << shift;
		break;
	case 1:
		result = (int32_t)value << shift;
		break;
	case 2:
		result = value >> shift;
		break;
	case 3:
		result = (int32_t)value >> shift;
		break;
	}
	set_reg(state, operands.d, result);
	state->pc += 4;
	return mips_Success;
}

mips_error shift(mips_cpu_h state, rtype operands)
{
	return shift_base(state, operands, operands.shift);
}

mips_error shift_var(mips_cpu_h state, rtype operands)
{
	return shift_base(state, operands, state->reg[operands.s1]);
}

mips_error jr(mips_cpu_h state, rtype operands)
{
	state->pc = state->reg[operands.s1];
	return mips_Success;
}

mips_error breakpoint(mips_cpu_h state, rtype operands)
{
	return mips_ExceptionBreak;
}

mips_error mfhi(mips_cpu_h state, rtype operands)
{
	state->reg[operands.d] = state->hi_lo.parts.hi;
	state->pc += 4;
	return mips_Success;
}

mips_error mthi(mips_cpu_h state, rtype operands)
{
	state->hi_lo.parts.hi = state->reg[operands.s1];
	state->pc += 4;
	return mips_Success;
}

mips_error mflo(mips_cpu_h state, rtype operands)
{
	state->reg[operands.d] = state->hi_lo.parts.lo;
	state->pc += 4;
	return mips_Success;
}

mips_error mtlo(mips_cpu_h state, rtype operands)
{
	state->hi_lo.parts.lo = state->reg[operands.s1];
	state->pc += 4;
	return mips_Success;
}

mips_error add_sub(mips_cpu_h state, rtype operands)
{
	uint32_t* regs = state->reg;
	uint32_t result;
	uint32_t v1 = regs[operands.s1];
	uint32_t v2 = regs[operands.s2];
	int32_t x, y;
	switch(operands.f & 3)
	{
	case 0:
		x = (int32_t)v1;
		y = (int32_t)v2;
		if ((y > 0 && x > INT_MAX - y) ||
			(y < 0 && x < INT_MIN - y))
			return mips_ExceptionArithmeticOverflow;
	case 1:
		result = v1 + v2;
		break;
	case 2:
		result = ((int32_t)v1 - (int32_t)v2);
		break;
	case 3:
		result = v1 - v2;
	}
	set_reg(state, operands.d, result);
	state->pc += 4;
	return mips_Success;
}

mips_error mult(mips_cpu_h state, rtype operands)
{
	uint32_t* regs = state->reg;
	uint32_t v1 = regs[operands.s1];
	uint32_t v2 = regs[operands.s2];
	if(operands.f & 1)
		state->hi_lo.full = (uint64_t)v1 * (uint64_t)v2;
	else
		state->hi_lo.full = (uint64_t)((int64_t)v1 * (int64_t)v2);
	state->pc += 4;
	return mips_Success;
}

mips_error _div(mips_cpu_h state, rtype operands)
{
	uint32_t* regs = state->reg;
	uint32_t v1 = regs[operands.s1];
	uint32_t v2 = regs[operands.s2];
	if(operands.f & 1)
	{
		state->hi_lo.parts.lo = v1 / v2;
		state->hi_lo.parts.hi = v1 & v2;
	}
	else
	{
		state->hi_lo.parts.lo = (uint32_t)((int32_t)v1 / (int32_t)v2);
		state->hi_lo.parts.hi = (uint32_t)((int32_t)v1 & (int32_t)v2);
	}
	state->pc += 4;
	return mips_Success;
}

mips_error bitwise(mips_cpu_h state, rtype operands)
{
	uint32_t* regs = state->reg;
	uint32_t v1 = regs[operands.s1];
	uint32_t v2 = regs[operands.s2];
	uint32_t result;
	switch(operands.f)
	{
	case 0:
		result = v1 & v2;
		break;
	case 1:
		result = v1 | v2;
		break;
	case 2:
		result = v1 ^ v2;
		break;
	case 3:
		result = ~(v1 | v2);
		break;
	default:
		return mips_ExceptionInvalidInstruction;
	}
	set_reg(state, operands.d, result);
	state->pc += 4;
	return mips_Success;
}

mips_error slt(mips_cpu_h state, rtype operands)
{
	uint32_t* regs = state->reg;
	uint32_t v1 = regs[operands.s1];
	uint32_t v2 = regs[operands.s2];
	bool result;
	if(operands.f & 1)
		result = (int32_t)v1 < (int32_t)v2;
	else
		result = v1 < v2;
	set_reg(state, operands.d, (uint32_t)result);
	state->pc += 4;
	return mips_Success;
}

/// Map of R-type 'function' fields to function pointers
static rtype_op rtype_ops[64] =
{
	/// 0000
	&shift,
	NULL,
	&shift,
	&shift,
	/// 0001
	&shift_var,
	NULL,
	&shift_var,
	NULL,
	/// 0010
	&jr,
	NULL,
	NULL,
	NULL,
	/// 0011
	&not_impl_r,
	&breakpoint,
	NULL,
	NULL,
	/// 0100
	&mfhi,
	&mthi,
	&mflo,
	&mtlo,
	/// 0101
	BLANK,
	/// 0110
	&mult,
	&mult,
	&_div,
	&_div,
	/// 0111
	BLANK,
	/// 1000
	&add_sub,
	&add_sub,
	&add_sub,
	&add_sub,
	/// 1001
	&bitwise,
	&bitwise,
	&bitwise,
	&bitwise,
	/// 1010
	BLANK,
	/// 1011
	&slt,
	&slt,
	NULL,
	NULL,
	/// 1100
	BLANK,
	BLANK,
	BLANK,
	BLANK
};

/// Most r-type instructions have opcode 0, but a separate 'function' field
/// This function handles that
mips_error do_rtype_op(mips_cpu_h state, uint32_t instruction)
{
	rtype operands;
	operands = get_rtype(instruction);
	rtype_op rtop = rtype_ops[operands.f];
	if(rtop == NULL)
		return mips_ExceptionInvalidInstruction;
	return rtop(state, operands);
}

/// The map of opcodes (6 bits) to operation function pointers
/// A value of NULL here will throw a mips_ErrorInvalidInstruction
static op operations[64] =
{
	/// 0000
	&do_rtype_op,
	&branch_zero,
	&jump,
	&jal,
	/// 0001
	&branch_var,
	&branch_var,
	&branch_zero,
	&branch_zero,
	/// 0010
	&addi,
	&addi,
	&slti,
	&slti,
	/// 0011
	&bitwise_imm,
	&bitwise_imm,
	&bitwise_imm,
	&lui,
	/// 0100 - COPz
	&not_impl,
	&not_impl,
	&not_impl,
	&not_impl,
	/// 0101
	BLANK,
	/// 0110
	BLANK,
	/// 0111
	BLANK,
	/// 1000 - signed load
	&lb,
	&lh,
	&lwl,
	&lw,
	/// 1001 - unsigned load
	&lb,
	&lh,
	&lwr,
	NULL,
	/// 1010
	&sb,
	&sh,
	NULL,
	&sw,
	/// 1011
	BLANK,
	/// 1100 - LWCz
	&not_impl,
	&not_impl,
	&not_impl,
	&not_impl,
	/// 1101
	BLANK,
	/// 1110 - SWCz
	&not_impl,
	&not_impl,
	&not_impl,
	&not_impl,
	/// 1111
	BLANK
};

mips_cpu_h mips_cpu_create(mips_mem_h mem)
{
	mips_cpu_h ret = malloc(sizeof(struct mips_cpu_impl));
	ret->mem = mem;
	mips_cpu_reset(ret);
	return ret;
}

mips_error mips_cpu_reset(mips_cpu_h state)
{
	if(state == NULL)
		return mips_ErrorInvalidHandle;
	state->pc = 0;
	state->hi_lo.full = 0;
	uint32_t* ptr = state->reg;
	uint32_t* end = ptr + 32;
	while(ptr < end)
		*(ptr++) = 0;
	return mips_Success;
}

mips_error mips_cpu_get_register(
	mips_cpu_h state,
	unsigned index,
	uint32_t *value
)
{
	if(state == NULL)
		return mips_ErrorInvalidHandle;
	if(index >= NUM_REGS || value == NULL)
		return mips_ErrorInvalidArgument;
	*value = index ? state->reg[index] : 0;
	return mips_Success;
}

mips_error mips_cpu_set_pc(
	mips_cpu_h state,
	uint32_t pc
)
{
	if(state == NULL)
		return mips_ErrorInvalidHandle;
	state->pc = pc;
	return mips_Success;
}

mips_error mips_cpu_get_pc(mips_cpu_h state, uint32_t *pc)
{
	if(state == NULL)
		return mips_ErrorInvalidHandle;
	if(pc == NULL)
		return mips_ErrorInvalidArgument;
	*pc = state->pc;
	return mips_Success;
}

mips_error mips_cpu_step(mips_cpu_h state)
{
	static uint32_t instruction;
	if(state == NULL || state->mem == NULL)
		return mips_ErrorInvalidHandle;
	mips_error memresult = mips_mem_read(
		state->mem,
		state->pc,
		sizeof(instruction),
		(uint8_t*)&instruction);
	if(memresult != mips_Success)
		return memresult;

	unsigned opcode = instruction >> 26;
	op opr = operations[opcode];
	if(opr == NULL)
		return mips_ExceptionInvalidInstruction;
	mips_error ret = opr(state, instruction);
	if(!ret)
		state->pc += sizeof(uint32_t);
	return ret;
}

mips_error mips_cpu_set_debug_level(mips_cpu_h state,
	unsigned level,
	FILE *dest)
{
	if(state == NULL)
		return mips_ErrorInvalidHandle;
	state->debug = level;
	state->output = dest;
	return mips_Success;
}

void mips_cpu_free(mips_cpu_h state)
{
	if(state != NULL)
	{
		if(state->output != NULL)
			fclose(state->output);
		if(state->mem != NULL)
			mips_mem_free(state->mem);
		free(state);
	}
}

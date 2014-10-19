/**
 * MIPS-I CPU Implementation
 * (C) Hamish Milne 2014
 *
 * Implements all MIPS-I instructions
 *
 * Compiled under standard GCC (C90 with single line comments)
 **/

#include "mips_cpu.h"
#include "stdio.h"
#include "limits.h"
#include "stdbool.h"
#include "string.h"

#define NUM_REGS 32
#define BUF_SIZE 256

#define BLANK {0},{0},{0},{0}

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

/// Signature for an R-type operation
/// 'state' can be assumed valid
typedef mips_error (*rtype_op)(mips_cpu_h state, rtype operands);

/// Signature for a state flag operation
/// 'state' can be assumed valid
typedef mips_error (*state_op)(mips_cpu_h state);

/// Contains an operation function and its name
typedef struct
{
	op op;
	const char* name;
} op_info;

/// Contains an R-type function and its name
typedef struct
{
	rtype_op op;
	const char* name;
} rtype_op_info;

typedef struct
{
	uint16_t value;
	unsigned dest;
	bool shift;
} lw_data;

/// CPU state structure
struct mips_cpu_impl
{
	/// Pointer to memory object
	mips_mem_h mem;
	/// Debug level
	unsigned debug;
	/// Output for debug messages
	FILE* output;
	/// Debug handler method
	debug_handle debug_handle;
	/// Exception handler locations
	uint32_t exception[16];
	/// Program counter
	uint32_t pc;
	/// Delay state
	unsigned delay;
	/// Delayed jump address
	uint32_t jump_addr;
	/// Saved values for LWL and LWR
	lw_data lw1, lw2;
	/// The $HI and $LO registers
	long_reg hi_lo;
	/// Coprocessor settings
	coprocessor coprocessor[4];
	/// General purpose registers
	uint32_t reg[NUM_REGS];
	/// Flags for when the register is undefined
	bool undefined[NUM_REGS];
};

/// Parses an R-type operand list from an instruction
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

/// Parses an I-type operand list from an instruction
itype get_itype(uint32_t instr)
{
	itype ret;
	unsigned* val = (unsigned*)&ret;
	*val++ = instr >> 26;
	*val++ = (instr >> 21) & 0x1F;
	*val++ = (instr >> 16) & 0x1F;
	*val++ = (int16_t)(instr & 0xFFFF);
	return ret;
}

/// Parses a J-type operand list from an instruction
jtype get_jtype(uint32_t instr)
{
	jtype ret;
	ret.opcode = instr >> 26;
	ret.imm = instr & 0x3FFFFFF;
	return ret;
}

/// Reverses the byte order of the given input
uint32_t reverse_word(uint32_t word)
{
	uint32_t ret;
	uint8_t *ip = (uint8_t*)&word + 4;
	uint8_t *op = (uint8_t*)&ret;
	uint8_t *end = op + 4;
	while(op < end)
		*op++ = *--ip;
	return ret;
}

/// Reverses the byte order of the given input
uint16_t reverse_half(uint16_t half)
{
	uint16_t ret;
	uint8_t *ip = (uint8_t*)&half;
	uint8_t *op = (uint8_t*)&ret;
	*op = *(ip + 1);
	*(op + 1) = *ip;
	return ret;
}

/// Reverses the byte order of the given input
void reverse_data(uint8_t* data, unsigned length)
{
	unsigned begin = 0;
	uint8_t tmp;
	while(--length > begin)
	{
		tmp = data[begin];
		data[begin++] = data[length];
		data[length] = tmp;
	}
}

const char* exceptions[16] =
{
	"Break",
	"Invalid address",
	"Invalid alignment",
	"Access violation",
	"Invalid instruction",
	"Arithmetic overflow",
	"Coprocessor unusable",
	"System call",
	0,0,0,0,
	0,0,0,0
};

static char temp_buf[BUF_SIZE];

void debug(mips_cpu_h state, const char* buf, size_t bufsize)
{
	debug_handle dh = state->debug_handle;
	if(dh == NULL)
	{
		FILE* file = state->output;
		if(file == NULL)
			file = stdout;
		fwrite(buf, 1, bufsize, file);
	}
	else
	{
		dh(state, buf, bufsize);
	}
}

/// Sets a register, ensuring that $0 == 0 and outputting debug information
void set_reg(mips_cpu_h state, unsigned index, uint32_t value)
{
	state->reg[index] = index ? value : 0;
	if(state->debug > 1)
		debug(state, temp_buf, sprintf(temp_buf, "$%d = %d (0x%x)", index, (int32_t)value, value) + 1);
}

void set_branch_delay(mips_cpu_h state, uint32_t value)
{
	if(state->debug > 2)
		debug(state, temp_buf, sprintf(temp_buf, "$delay = 0x%x\n", value));
	state->delay = (state->delay & ~0x2) | 0x1;
	state->jump_addr = value;
}

void link(mips_cpu_h state, unsigned opcode, unsigned bit)
{
	if(opcode & bit)
		set_reg(state, 31, state->pc + 8);
}

/// Jump (and link)
mips_error jump(mips_cpu_h state, uint32_t instruction)
{
	jtype operands = get_jtype(instruction);
	link(state, operands.opcode, 1);
	state->pc += 4;
	set_branch_delay(state, (state->pc & 0xF0000000) | ((int16_t)operands.imm << 2));
	return mips_Success;
}

/// General function for all instructions that
/// branch on condition, comparing to zero
mips_error branch_zero(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	int32_t value = state->reg[operands.s];
	bool result;
	/// Combine the opcode and the final bit of the 'd' field
	/// Use to determine exact instruction
	char* fmt;
	switch((operands.opcode << 1) + (operands.d & 1))
	{
	case 2: /// BLTZ
		result = value < 0;
		fmt = "Test: $%d %d < 0\n";
		break;
	case 3: /// BGEZ
		result = value >= 0;
		fmt = "Test: $%d %d >= 0\n";
		break;
	case 12: /// BLEZ
		result = value <= 0;
		fmt = "Test: $%d %d <= 0\n";
		break;
	case 14: /// BGTZ
		result = value > 0;
		fmt = "Test: $%d %d > 0\n";
		break;
	default:
		return mips_ExceptionInvalidInstruction;
	}
	if(state->debug > 2)
	{
		debug(state, temp_buf, sprintf(temp_buf, fmt, operands.s, value));
	}
	/// Use the first bit of the 'd' field
	/// to determine if we need to link
	/// The link happens regardless of whether the condition is true
	link(state, operands.d, 0x20);
	state->pc += 4;
	if(result)
		set_branch_delay(state, (int16_t)operands.imm << 2);
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
	if(state->debug > 2)
	{
		debug(state, temp_buf, sprintf(temp_buf,
				"Test: $%d %c= $%d - %s\n", operands.s,
				(operands.opcode & 1) ? '!' : '=',
				operands.d, result ? "TRUE" : "FALSE"));
	}
	if(result)
		set_branch_delay(state, ((int16_t)operands.imm << 2) + 4);
	state->pc += 4;
	return mips_Success;
}

/// Add immediate
mips_error addi(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	uint32_t value = state->reg[operands.s];
	if(state->debug > 2)
	{
		debug(state, temp_buf, sprintf(temp_buf,
				"$%d = $%d + %d\n", operands.d, operands.s,
				(int16_t)operands.imm));
	}
	uint32_t result;
	if(!(operands.opcode & 1))
	{
		int32_t x = value;
		int32_t y = (int16_t)operands.imm;
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

/// Set if less than immediate
mips_error slti(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	uint32_t value = state->reg[operands.s];
	bool result;
	if(operands.opcode & 1)
		result = value < operands.imm;
	else
		result = (int32_t)value < (int16_t)operands.imm;
	if(state->debug > 2)
	{
		debug(state, temp_buf, sprintf(temp_buf,
				"Test ($%d) %d < %d - %s\n", operands.s, value,
				(int16_t)operands.imm, result ? "TRUE" : "FALSE"));
	}
	set_reg(state, operands.d, (uint32_t)result);
	state->pc += 4;
	return mips_Success;
}

/// Bitwise functions with immediate
mips_error bitwise_imm(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	uint32_t value = state->reg[operands.s];
	uint32_t result;
	char c;
	switch(operands.opcode & 3)
	{
	case 0: /// ANDI
		result = value & operands.imm;
		c = '&';
		break;
	case 1: /// ORI
		result = value | operands.imm;
		c = '|';
		break;
	case 2: /// XORI
		result = value ^ operands.imm;
		c = '^';
		break;
	default:
		return mips_ExceptionInvalidInstruction;
	}
	if(state->debug > 2)
	{
		debug(state, temp_buf, sprintf(temp_buf,
				"$%d = $%d %c 0x%x\n", operands.d,
				operands.s, c, operands.imm));
	}
	set_reg(state, operands.d, result);
	state->pc += 4;
	return mips_Success;
}

/// Load upper immediate
mips_error lui(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	set_reg(state, operands.d, operands.imm << 16);
	if(state->debug > 2)
	{
		debug(state, temp_buf, sprintf(temp_buf,
				"$%d = 0x%x\n", operands.d,
				operands.imm << 16));
	}
	state->pc += 4;
	return mips_Success;
}

/// Common function for most memory operations
mips_error mem_base(mips_cpu_h state, itype operands, bool load, int length, uint8_t* word, int offset)
{
	if(state->mem == NULL)
		return mips_ErrorInvalidHandle;
	mips_error error;
	printf("%x %d %x %d\n", operands.imm, operands.s, (int16_t)operands.imm, (int16_t)operands.imm);
	uint32_t addr = state->reg[operands.s] + (int16_t)operands.imm + offset;
	if(state->debug > 2)
	{
		if(load)
			debug(state, temp_buf, sprintf(temp_buf,
				"$%d = mem[0x%x : 0x%x]\n",
				operands.d, addr, addr + length - 1));
		else
			debug(state, temp_buf, sprintf(temp_buf,
				"mem[0x%x : 0x%x] = $%d\n",
				addr, addr + length - 1, operands.d));
	}
	if(load)
		error = mips_mem_read(state->mem, addr, length, word);
	else
		error = mips_mem_write(state->mem, addr, length, word);
	return error;
}

/// Coprocessor instruction
mips_error copz(mips_cpu_h state, uint32_t instruction)
{
	op cop = state->coprocessor[(instruction >> 26) & 3].cop;
	if(cop == NULL)
		return mips_ErrorNotImplemented;
	if(state->debug > 2)
	{
		debug(state, temp_buf, sprintf(temp_buf,
				"    0x%x", instruction & 0x3FFFFFF));
	}
	return cop(state, instruction);
}

/// Load word to a coprocessor
mips_error lwcz(mips_cpu_h state, uint32_t instruction)
{
	cop_load_store lwc = state->coprocessor[(instruction >> 26) & 3].lwc;
	if(lwc == NULL)
		return mips_ErrorNotImplemented;
	uint32_t data;
	itype operands = get_itype(instruction);
	if(state->debug > 2)
	{
		debug(state, temp_buf, sprintf(temp_buf, "CP%d: ",
				(instruction >> 26) & 3));
	}
	mips_error error = mem_base(state, operands, true, 4, (uint8_t*)&data, 0);
	if(error)
		return error;
	return lwc(state, operands.d, &data);
}

/// Store word from a coprocessor
mips_error swcz(mips_cpu_h state, uint32_t instruction)
{
	cop_load_store lwc = state->coprocessor[(instruction >> 26) & 3].swc;
	if(lwc == NULL)
		return mips_ErrorNotImplemented;
	uint32_t data;
	itype operands = get_itype(instruction);
	mips_error error = lwc(state, operands.d, &data);
	if(error)
		return error;
	if(state->debug > 2)
	{
		debug(state, temp_buf, sprintf(temp_buf, "CP%d: ",
				(instruction >> 26) & 3));
	}
	return mem_base(state, operands, true, 4, (uint8_t*)&data, 0);
}

/// Load byte
mips_error lb(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	int8_t word;
	mips_error error = mem_base(state, operands, true, 1, (uint8_t*)&word, 0);
	if(error)
		return error;
	/// The fourth bit of the opcode determines whether to extend the sign bit
	set_reg(state, operands.d, (operands.opcode & 4) ? (uint8_t)word : word);
	state->pc += 4;
	return mips_Success;
}

/// Load half word
mips_error lh(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	int16_t word;
	mips_error error = mem_base(state, operands, true, 1, (uint8_t*)&word, 0);
	if(error)
		return error;
	/// The fourth bit of the opcode determines whether to extend the sign bit
	set_reg(state, operands.d, (operands.opcode & 4) ? (uint16_t)word : word);
	state->pc += 4;
	return mips_Success;
}

/// Load word
mips_error lw(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	uint32_t word;
	mips_error error = mem_base(state, operands, true, 4, (uint8_t*)&word, 0);
	if(error)
		return error;
	set_reg(state, operands.d, word);
	state->pc += 4;
	return mips_Success;
}

/// Sets the delay state to execute a LWX instruction
void set_lw_delay(mips_cpu_h state, unsigned dest, uint16_t value, bool shift)
{
	lw_data data;
	data.dest = dest;
	data.value = value;
	data.shift = shift;
	if(state->delay & 0xC)
	{
		state->delay |= 0x10;
		state->lw2 = data;
	}
	else
	{
		state->delay |= 0x4;
		state->lw1 = data;
	}
}

/// Load word left
mips_error lwl(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	uint16_t word;
	mips_error error = mem_base(state, operands, true, 2, (uint8_t*)&word, 0);
	if(error)
		return error;
	set_reg(state, operands.d, state->reg[operands.d] & 0x0000FFFF);
	set_lw_delay(state, operands.d, word, true);
	state->pc += 4;
	return mips_Success;
}

/// Load word right
mips_error lwr(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	uint16_t word;
	mips_error error = mem_base(state, operands, true, 2, (uint8_t*)&word, -1);
	if(error)
		return error;
	set_reg(state, operands.d, state->reg[operands.d] & 0xFFFF0000);
	set_lw_delay(state, operands.d, word, true);
	state->pc += 4;
	return mips_Success;
}

/// Store byte
mips_error sb(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	uint8_t word = state->reg[operands.d];
	mips_error error = mem_base(state, operands, false, 1, &word, 0);
	if(error)
		return error;
	state->pc += 4;
	return mips_Success;
}

/// Store half word
mips_error sh(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	uint16_t word = state->reg[operands.d];
	mips_error error = mem_base(state, operands, false, 1, (uint8_t*)&word, 0);
	if(error)
		return error;
	state->pc += 4;
	return mips_Success;
}

/// Store word
mips_error sw(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	uint32_t word = state->reg[operands.d];
	mips_error error = mem_base(state, operands, false, 4, (uint8_t*)&word, 0);
	if(error)
		return error;
	state->pc += 4;
	return mips_Success;
}

/// Store word left
mips_error swl(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	uint16_t word = state->reg[operands.d] >> 16;
	mips_error error = mem_base(state, operands, false, 2, (uint8_t*)&word, 0);
	if(error)
		return error;
	state->pc += 4;
	return mips_Success;
}

/// Store word right
mips_error swr(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	uint16_t word = state->reg[operands.d];
	mips_error error = mem_base(state, operands, false, 2, (uint8_t*)&word, -1);
	if(error)
		return error;
	state->pc += 4;
	return mips_Success;
}

/// Base functionality for shift instructions
mips_error shift_base(mips_cpu_h state, rtype operands, uint32_t shift)
{
	uint32_t* regs = state->reg;
	uint32_t result;
	uint32_t value = regs[operands.s2];
	switch(operands.f & 3)
	{
	case 0: /// Logical left
		result = value << shift;
		break;
	case 1: /// Arithmetic left (not really used)
		result = (int32_t)value << shift;
		break;
	case 2: /// Logical right
		result = value >> shift;
		break;
	case 3: /// Arithmetic right
		result = (int32_t)value >> shift;
		break;
	}
	set_reg(state, operands.d, result);
	state->pc += 4;
	return mips_Success;
}

/// Shift by immediate (shift field)
mips_error shift(mips_cpu_h state, rtype operands)
{
	return shift_base(state, operands, operands.shift);
}

/// Shift variable
mips_error shift_var(mips_cpu_h state, rtype operands)
{
	return shift_base(state, operands, state->reg[operands.s1]);
}

/// Jump to register (and link)
mips_error jr(mips_cpu_h state, rtype operands)
{
	if(operands.f & 1)
		set_reg(state, operands.d, state->pc + 8);
	uint32_t val = state->reg[operands.s1];
	if(val & 0x3)
		return mips_ExceptionInvalidAlignment;
	set_branch_delay(state, val);
	state->pc += 4;
	return mips_Success;
}

/// System call
mips_error syscall(mips_cpu_h state, rtype operands)
{
	return mips_ExceptionSystemCall;
}

/// Break
mips_error breakpoint(mips_cpu_h state, rtype operands)
{
	return mips_ExceptionBreak;
}

/// Move from HI
mips_error mfhi(mips_cpu_h state, rtype operands)
{
	state->reg[operands.d] = state->hi_lo.parts.hi;
	state->pc += 4;
	return mips_Success;
}

/// Move to HI
mips_error mthi(mips_cpu_h state, rtype operands)
{
	state->hi_lo.parts.hi = state->reg[operands.s1];
	state->pc += 4;
	return mips_Success;
}

/// Move from LO
mips_error mflo(mips_cpu_h state, rtype operands)
{
	state->reg[operands.d] = state->hi_lo.parts.lo;
	state->pc += 4;
	return mips_Success;
}

/// Move to LO
mips_error mtlo(mips_cpu_h state, rtype operands)
{
	state->hi_lo.parts.lo = state->reg[operands.s1];
	state->pc += 4;
	return mips_Success;
}

/// Add or subtract registers
mips_error add_sub(mips_cpu_h state, rtype operands)
{
	if(state->debug > 2)
	{
		debug(state, temp_buf, sprintf(temp_buf,
				"$%d = $%d %c $%d\n", operands.d, operands.s1,
				(operands.f & 2) ? '-' : '+', operands.s2));
	}
	uint32_t* regs = state->reg;
	uint32_t v1 = regs[operands.s1];
	uint32_t v2 = regs[operands.s2];
	if(operands.f & 2)
		v2 = -v2;
	int32_t x = v1;
	int32_t y = v2;
	if(!(operands.f & 1))
		if ((y > 0 && x > INT_MAX - y) ||
			(y < 0 && x < INT_MIN - y))
			return mips_ExceptionArithmeticOverflow;
	set_reg(state, operands.d, v1 + v2);
	state->pc += 4;
	return mips_Success;
}

/// Multiply
mips_error mult(mips_cpu_h state, rtype operands)
{
	uint32_t* regs = state->reg;
	uint32_t v1 = regs[operands.s1];
	uint32_t v2 = regs[operands.s2];
	/// The last bit of the function field determines
	/// whether to use unsigned numbers
	if(operands.f & 1)
		state->hi_lo.full = (uint64_t)v1 * (uint64_t)v2;
	else
		state->hi_lo.full = (uint64_t)((int64_t)v1 * (int64_t)v2);
	state->pc += 4;
	return mips_Success;
}

/// Divide ('div' was already taken by stdlib.h)
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

/// Bitwise instructions
mips_error bitwise(mips_cpu_h state, rtype operands)
{
	uint32_t* regs = state->reg;
	uint32_t v1 = regs[operands.s1];
	uint32_t v2 = regs[operands.s2];
	uint32_t result;
	switch(operands.f)
	{
	case 0: /// AND
		result = v1 & v2;
		break;
	case 1: /// OR
		result = v1 | v2;
		break;
	case 2: /// XOR
		result = v1 ^ v2;
		break;
	case 3: /// NOR
		result = ~(v1 | v2);
		break;
	default:
		return mips_ExceptionInvalidInstruction;
	}
	set_reg(state, operands.d, result);
	state->pc += 4;
	return mips_Success;
}

/// Set if less than
mips_error slt(mips_cpu_h state, rtype operands)
{
	uint32_t* regs = state->reg;
	uint32_t v1 = regs[operands.s1];
	uint32_t v2 = regs[operands.s2];
	bool result;
	/// The last bit of the function field
	/// indicates whether to use an unsigned comparison
	if(operands.f & 1)
		result = (int32_t)v1 < (int32_t)v2;
	else
		result = v1 < v2;
	set_reg(state, operands.d, (uint32_t)result);
	state->pc += 4;
	return mips_Success;
}

/// Map of R-type 'function' fields to function pointers
static rtype_op_info rtype_ops[64] =
{
	/// 0000
	{ &shift, "SLL" },
	{ NULL },
	{ &shift, "SRL" },
	{ &shift, "SRA" },
	/// 0001
	{ &shift_var, "SLLV" },
	{ NULL },
	{ &shift_var, "SRLV" },
	{ &shift_var, "SRAV" },
	/// 0010
	{ &jr, "JR" },
	{ &jr, "JALR" },
	{ NULL },
	{ NULL },
	/// 0011
	{ &syscall, "SYSCALL" },
	{ &breakpoint, "BREAK" },
	{ NULL },
	{ NULL },
	/// 0100
	{ &mfhi, "MFHI" },
	{ &mthi, "MTHI" },
	{ &mflo, "MFLO" },
	{ &mtlo, "MTLO" },
	/// 0101
	BLANK,
	/// 0110
	{ &mult, "MULT" },
	{ &mult, "MULTU" },
	{ &_div, "DIV" },
	{ &_div, "DIVU" },
	/// 0111
	BLANK,
	/// 1000
	{ &add_sub, "ADD" },
	{ &add_sub, "ADDU" },
	{ &add_sub, "SUB" },
	{ &add_sub, "SUBU" },
	/// 1001
	{ &bitwise, "AND" },
	{ &bitwise, "OR" },
	{ &bitwise, "XOR" },
	{ &bitwise, "NOR" },
	/// 1010
	BLANK,
	/// 1011
	{ &slt, "SLT" },
	{ &slt, "SLTU" },
	{ NULL },
	{ NULL },
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
	rtype_op_info rtop = rtype_ops[operands.f];
	if(rtop.op == NULL)
		return mips_ExceptionInvalidInstruction;
	if(state->debug > 1)
	{
		const char* name = rtop.name;
		if(name == NULL)
			name = "Invalid instruction";
		debug(state, temp_buf, sprintf(temp_buf, "%s\n", name));
	}
	return rtop.op(state, operands);
}

/// The map of opcodes (6 bits) to operation function pointers
/// A value of NULL here will throw a mips_ErrorInvalidInstruction
static op_info operations[64] =
{
	/// 0000
	{ &do_rtype_op, "R-type:" },
	{ &branch_zero, "BLTZ/BGEZ" },
	{ &jump, "J" },
	{ &jump, "JAL" },
	/// 0001
	{ &branch_var, "BEQ" },
	{ &branch_var, "BNE" },
	{ &branch_zero, "BLEZ" },
	{ &branch_zero, "BGTZ" },
	/// 0010
	{ &addi, "ADDI" },
	{ &addi, "ADDIU" },
	{ &slti, "SLTI" },
	{ &slti, "SLTIU" },
	/// 0011
	{ &bitwise_imm, "ANDI" },
	{ &bitwise_imm, "ORI" },
	{ &bitwise_imm, "XORI" },
	{ &lui, "LUI" },
	/// 0100
	{ &copz, "COP0" },
	{ &copz, "COP1" },
	{ &copz, "COP2" },
	{ &copz, "COP3" },
	/// 0101
	BLANK,
	/// 0110
	BLANK,
	/// 0111
	BLANK,
	/// 1000
	{ &lb, "LB" },
	{ &lh, "LH" },
	{ &lwl, "LWL" },
	{ &lw, "LW" },
	/// 1001
	{ &lb, "LBU" },
	{ &lh, "LHU" },
	{ &lwr, "LWR" },
	{ NULL },
	/// 1010
	{ &sb, "SB" },
	{ &sh, "SH" },
	{ &swl, "SWL" },
	{ &sw, "SW" },
	/// 1011
	{ NULL },
	{ NULL },
	{ &swr, "SWR" },
	{ NULL },
	/// 1100
	{ &lwcz, "LWC0" },
	{ &lwcz, "LWC1" },
	{ &lwcz, "LWC2" },
	{ &lwcz, "LWC3" },
	/// 1101
	BLANK,
	/// 1110
	{ &swcz, "SWC0" },
	{ &swcz, "SWC1" },
	{ &swcz, "SWC2" },
	{ &swcz, "SWC3" },
	/// 1111
	BLANK
};

/// Delay by one step
mips_error pre_jump(mips_cpu_h state)
{
	state->delay |= 0x2;
	return mips_Success;
}

/// Performs the actual jump
mips_error do_jump(mips_cpu_h state)
{
	if(state->debug > 2)
		debug(state, temp_buf, sprintf(temp_buf, "$PC = 0x%x\n", state->jump_addr));
	state->pc = state->jump_addr;
	return mips_Success;
}

/// Performs the action described by the lw_data object
mips_error do_lwdata(mips_cpu_h state, lw_data data)
{
	uint32_t value = data.value;
	if(data.shift)
		value <<= 16;
	set_reg(state, data.dest, value | state->reg[data.dest]);
	return mips_Success;
}

/// Delay by one step
mips_error pre_lw1(mips_cpu_h state)
{
	state->delay |= 0x8;
	return mips_Success;
}

/// Performs the first queued LWX instruction
mips_error do_lw1(mips_cpu_h state)
{
	return do_lwdata(state, state->lw1);
}

/// Delay by one step
mips_error pre_lw2(mips_cpu_h state)
{
	state->delay |= 0x20;
	return mips_Success;
}

/// Performs the second queued LWX instruction
mips_error do_lw2(mips_cpu_h state)
{
	return do_lwdata(state, state->lw2);
}

/// Map of state flags to functions
static state_op state_ops[32] =
{
	&pre_jump,
	&do_jump,
	&pre_lw1,
	&do_lw1,
	&pre_lw2,
	&do_lw2,
	NULL,
	NULL,
	0,0,0,0,
	0,0,0,0,
	0,0,0,0,
	0,0,0,0,
	0,0,0,0,
	0,0,0,0
};

static struct mips_cpu_impl cpu_empty = {0};

/// Creates a CPU state
mips_cpu_h mips_cpu_create(mips_mem_h mem)
{
	mips_cpu_h ret = malloc(sizeof(struct mips_cpu_impl));
	*ret = cpu_empty;
	ret->mem = mem;
	return ret;
}

/// Resets the CPU to zero
mips_error mips_cpu_reset(mips_cpu_h state)
{
	if(state == NULL)
		return mips_ErrorInvalidHandle;
	mips_mem_h mem = state->mem;
	unsigned debug = state->debug;
	debug_handle dh = state->debug_handle;
	*state = cpu_empty;
	state->mem = mem;
	state->debug = debug;
	state->debug_handle = dh;
	return mips_Success;
}

/// Gets a register value
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

/// Sets the program counter
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

/// Gets the program counter
mips_error mips_cpu_get_pc(mips_cpu_h state, uint32_t *pc)
{
	if(state == NULL)
		return mips_ErrorInvalidHandle;
	if(pc == NULL)
		return mips_ErrorInvalidArgument;
	*pc = state->pc;
	return mips_Success;
}

/// Performs one step in the CPU
mips_error mips_cpu_step(mips_cpu_h state)
{
	static uint32_t instruction;
	if(state == NULL || state->mem == NULL)
		return mips_ErrorInvalidHandle;

	unsigned delay = state->delay;
	mips_error ret;
	int i;
	for(i = 0; i < 32; i++)
		if(delay & (1 << i))
		{
			state_op sop = state_ops[i];
			//if(state->debug > 3)
			//	debug(state, temp_buf, sprintf(temp_buf, "State flag %d is set\n", i));
			if(sop != NULL)
			{
				state->delay = delay & ~(1 << i);
				ret = sop(state);
				if(ret != mips_Success)
					return ret;
			}
		}

	printf("PC: %d\n", state->pc);
	mips_error memresult = mips_mem_read(
		state->mem,
		state->pc,
		sizeof(instruction),
		(uint8_t*)&instruction);
	if(memresult != mips_Success)
		return memresult;

	instruction = reverse_word(instruction);
	unsigned opcode = instruction >> 26;
	op_info opinfo = operations[opcode];
	if(opinfo.op == NULL)
		return mips_ExceptionInvalidInstruction;

	if(state->debug > 1 && opcode > 0)
	{
		const char* name = opinfo.name;
		if(name == NULL)
			name = "Unknown instruction";
		debug(state, temp_buf, sprintf(temp_buf, "%s\n", name));
	}

	return opinfo.op(state, instruction);
}

/// Sets the debug level:
///   0: None
///   1: Undefined registers
///   2: Instructions passed
///   3: Register setting
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

/// Assigns the given coprocessor object to the given index
mips_error mips_cpu_set_coprocessor(mips_cpu_h state,
	unsigned index,
	coprocessor cp)
{
	if(state == NULL)
		return mips_ErrorInvalidHandle;
	if(index > 3)
		return mips_ErrorInvalidArgument;
	state->coprocessor[index] = cp;
	return mips_Success;
}

/// Releases CPU resources
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

mips_error mips_load_file(mips_mem_h mem, const char* file)
{
	FILE* fp = fopen(file, "rb");
	if(fp == NULL || ferror(fp))
		return mips_ErrorFileReadError;
	fseek(fp, 0, SEEK_END);
	unsigned len = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	uint8_t* temp = malloc(len);
	fread(temp, 1, len, fp);
	mips_error error = mips_ErrorFileReadError;
	if(!ferror(fp))
	{
		//reverse_data(temp, len);
		error = mips_mem_write(mem, 0, len, temp);
	}

	free(temp);
	fclose(fp);
	return error;
}

int main()
{
	mips_cpu_h state = mips_cpu_create(mips_mem_create_ram(4096, 4));
	mips_cpu_set_debug_level(state, 10, NULL);
	uint32_t abc = 0xFFFFFFFF;
	printf("%d\n", -abc);
	mips_load_file(state->mem, "fragments/f_fibonacci-mips.bin");
	while(1)
	{
		if(mips_cpu_step(state))
			printf("ERROR\n");
		getchar();
		//getchar();
	}
	return 0;
}

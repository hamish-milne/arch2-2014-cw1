#include "mips_cpu.h"
#include "stdio.h"
#include "limits.h"

#define NUM_REGS 32

#define BLANK 0,0,0,0

struct s_hi_lo
{
	uint32_t hi, lo;
};

union long_reg
{
	uint64_t full;
	s_hi_lo parts;
};

struct mips_cpu_impl
{
	mips_mem_h mem;
	unsigned debug;
	FILE* output;
	uint32_t pc;
	long_reg hi_lo;
	uint32_t reg[NUM_REGS];
};

struct rtype
{
	unsigned opcode, s1, s2, d, shift, f;
};

struct itype
{
	unsigned opcode, s, d, imm;
};

struct jtype
{
	unsigned opcode, imm;
};

mips_error (*op)(mips_cpu_h state, uint32_t instruction);
mips_error (*rtype_op)(mips_cpu_h state, rtype operands);

static char temp_buf[64];

static op* operations[64]
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
	/// 0100
	BLANK,
	/// 0101
	BLANK,
	/// 0110
	BLANK,
	/// 0111
	BLANK,
	/// 1000
	&lb,
	&lh,
	&lwl,
	&lw,
	/// 1001
	&lb,
	&lh,
	&lwr,
	NULL,
	/// 1010
	&sb,
	NULL,
	NULL,
	&sw,
	/// 1011
	BLANK,
	BLANK,
	BLANK,
	BLANK,
	BLANK
};

static rtype_op* rtype_ops[64]
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
	&syscall,
	NULL,
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
	&div,
	&div,
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

void set_reg(mips_cpu_h state, unsigned index, uint32_t value)
{
	state->reg[index] = index ? value : 0;
	if(state->debug > 1 && state->output != NULL)
		fprintf(state->output, "$%d = %d (%a)", index, (int32_t)value, value);
}

mips_error do_rtype_op(mips_cpu_h state, uint32_t instruction)
{
	rtype operands = get_rtype(instruction);
	rtype_op* rtop = rtype_ops[operands.f];
	if(rtop == NULL)
		return mips_ExceptionInvalidInstruction;
	return rtop(state, operands);
}

mips_error jump(mips_cpu_h state, uint32_t instruction)
{
	jtype operands = get_jtype(instruction);
	state->pc = (state->pc & 0xF0000000) | (operands.imm << 2);
	return mips_Success;
}

mips_error jal(mips_cpu_h state, uint32_t instruction)
{
	jtype operands = get_jtype(instruction);
	set_reg(state, 31, state->pc + 4);
	state->pc = (state->pc & 0xF0000000) | (operands.imm << 2);
	return mips_Success;
}

mips_error branch_zero(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	int32_t value = (int32_t)state->reg[operands.s];
	bool result;
	switch((operands.opcode << 1) + (operands.d & 1))
	{
	case 2:
		result = value < 0;
		break;
	case 3:
		result = value >= 0;
		break;
	case 12:
		result = value <= 0;
		break;
	case 14:
		result = value > 0;
		break;
	default:
		return mips_ExceptionInvalidInstruction;
	}
	if(result)
	{
		if(operands.d & 0x10)
			set_reg(state, 31, state->pc + 4);
		state->pc += operands.imm << 2;
	}
	else
	{
		state->pc += 4;
	}
	return mips_Success;
}

mips_error branch_var(mips_cpu_h state, uint32_t instruction)
{
	uint32_t* regs = state->reg;
	itype operands = get_itype(instruction);
	bool result = regs[operands.s] == regs[operands.d];
	if(operands.opcode & 1)
		result = !result;
	state->pc += result ? operands.imm << 2 : 4;
	return mips_Success;
}

mips_error addi(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	uint32_t value = state->reg[operands.s];
	if(!(operands.opcode & 1))
	{
		int32_t x = (int32_t)value;
		int32_t y = (int32_t)operands.imm;
		if ((y > 0 && x > INT_MAX - y) ||
			(y < 0 && x < INT_MIN - y))
			return mips_ExceptionArithmeticOverflow;
	}
	set_reg(state, operands.d, value + operands.imm);
	state->pc += 4;
	return mips_Success;
}

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

mips_error mem_base(mips_cpu_h state, uint32_t instruction, bool load, int length, uint8_t* word)
{
	if(state->mem == NULL)
		return mips_ErrorInvalidHandle;
	mips_error error;
	uint32_t addr = state->regs[operands.s] + (int16_t)operands.imm;
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
	mips_error error = mem_base(state, instruction, true, 1, (uint8_t*)&word);
	if(error)
		return error;
	set_reg(state, operands.d, (operands.opcode & 4) ? (uint32_t)word : (int32_t)word)
	state->pc += 4;
	return mips_Success;
}

mips_error lh(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	int16_t word;
	mips_error error = mem_base(state, instruction, true, 1, (uint8_t*)&word);
	if(error)
		return error;
	set_reg(state, operands.d, (operands.opcode & 4) ? (uint32_t)word : (int32_t)word)
	state->pc += 4;
	return mips_Success;
}

mips_error lw(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	uint32_t word;
	return mem_base(state, instruction, true, 4, (uint8_t*)&word);
	if(error)
		return error;
	set_reg(state, operands.d, word)
	state->pc += 4;
	return mips_Success;
}

mips_error lwl(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	uint16_t word;
	mips_error error = mem_base(state, instruction, true, 1, (uint8_t*)&word);
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
	uint32_t addr = state->regs[operands.s] + (int16_t)operands.imm - 1;
	mips_error error = mips_mem_read(state->mem, addr, length, (uint8_t*)&word);
	if(error)
		return error;
	set_reg(state, operands.d, (uint32_t)word | (state->reg[operands.d] & 0xFFFF0000));
	state->pc += 4;
	return mips_Success;
}

mips_error sb(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	uint8_t word = (uint8_t)state->regs[operands.d];
	mips_error error = mem_base(state, instruction, false, 1, &word);
	if(error)
		return error;
	state->pc += 4;
	return mips_Success;
}

mips_error sh(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	uint16_t word = (uint8_t)state->regs[operands.d];
	mips_error error = mem_base(state, instruction, false, 1, (uint8_t*)&word);
	if(error)
		return error;
	state->pc += 4;
	return mips_Success;
}

mips_error sw(mips_cpu_h state, uint32_t instruction)
{
	itype operands = get_itype(instruction);
	uint32_t word = state->regs[operands.d];
	mips_error error = mem_base(state, instruction, false, 4, (uint8_t*)&word);
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
	return shift_base(state, operands, state->regs[operands.s1]);
}

mips_error jr(mips_cpu_h state, rtype operands)
{
	state->pc = state->regs[operands.s1];
	return mips_Success;
}

mips_error syscall(mips_cpu_h, rtype operands)
{
	return mips_ErrorNotImplemented;
}

mips_error mfhi(mips_cpu_h state, rtype operands)
{
	state->regs[operands.d] = state->hi_lo.parts.hi;
	state->pc += 4;
	return mips_Success;
}

mips_error mthi(mips_cpu_h state, rtype operands)
{
	state->hi_lo.parts.hi = state->regs[operands.s1];
	state->pc += 4;
	return mips_Success;
}

mips_error mflo(mips_cpu_h state, rtype operands)
{
	state->regs[operands.d] = state->hi_lo.parts.lo;
	state->pc += 4;
	return mips_Success;
}

mips_error mtlo(mips_cpu_h state, rtype operands)
{
	state->hi_lo.parts.lo = state->regs[operands.s1];
	state->pc += 4;
	return mips_Success;
}

mips_error add_sub(mips_cpu_h state, rtype operands)
{
	uint32_t* regs = state->reg;
	uint32_t result;
	uint32_t v1 = regs[operands.s1];
	uint32_t v2 = regs[operands.s2];
	switch(operands.f & 3)
	{
	case 0:
		int32_t x = (int32_t)v1;
		int32_t y = (int32_t)v2;
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

mips_error div(mips_cpu_h state, rtype operands)
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

mips_error bitwise(mips_cpu_state, rtype operands)
{
	uint32_t* regs = state->reg;
	uint32_t v1 = regs[operands.s1];
	uint32_t v2 = regs[operands.s2];
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

rtype get_rtype(uint32_t instr)
{
	rtype ret;
	unsigned* val = &ret;
	val++ = instr >> 26;
	val++ = (instr >> 21) & 0x1F;
	val++ = (instr >> 16) & 0x1F;
	val++ = (instr >> 11) & 0x1F;
	val++ = (instr >> 6) & 0x1F;
	val++ = instr & 0x3F;
	return ret;
}

itype get_itype(uint32_t instr)
{
	itype ret;
	unsigned* val = &ret;
	val++ = instr >> 26;
	val++ = (instr >> 21) & 0x1F;
	val++ = (instr >> 16) & 0x1F;
	val++ = instr & 0xFFFF;
	return ret;
}

jtype get_jtype(uint32_t instr)
{
	jtype ret;
	ret.opcode = instr >> 26;
	ret.imm = instr & 0x3FFFFFF;
	return ret;
}

mips_cpu_h mips_cpu_create(mips_mem_h mem)
{
	mips_cpu_h ret = malloc(sizeof(mips_cpu_impl));
	ret->mem = mem;
	mips_cpu_reset(ret);
	return ret;
}

mips_error mips_cpu_reset(mips_cpu_h state)
{
	if(state == NULL)
		return mips_ErrorInvalidHandle;
	ret->pc = 0;
	ret->hi_lo.full = 0;
	int* ptr = ret->regs;
	int* end = ptr + 32;
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
	if(mips_error != mips_Success)
		return mips_error;

	unsigned opcode = instruction >> 26;
	op* opr = op_types[opcode];
	if(opr == NULL)
		return mips_ExceptionInvalidInstruction;
	mips_error ret = opr(instruction);
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
			fclose(output);
		if(state->mem != NULL)
			mips_mem_free(state->mem);
		free(state);
	}
}

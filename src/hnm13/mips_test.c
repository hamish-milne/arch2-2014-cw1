/**
 * MIPS-I CPU Test bed
 * (C) Hamish Milne 2014
 *
 * Tests all specified instructions
 * Un-commenting the relevant lines allows
 * for all MIPS-I instructions to be tested
 *
 * ISO C90 compatible
 **/

#include "mips_test.h"
#include "mips_cpu.h"
#include "mips_util.h"
#include <limits.h>
#include <stdbool.h>

/**
 * Required signature for a general test operation
 * name : The instruction name
 * state : The CPU object
 * mem : The memory object
 * index : An optional integer parameter stored in the test settings
 **/
typedef void (*test_op)(const char* name, mips_cpu_h state, mips_mem_h mem, unsigned index);

/**
 * Required signature for an R-type specific operation
 * a : The first operand value
 * b : The second operand value
 * out : The recorded value from the CPU
 * imm : true if the second value is a 16-bit constant
 * error : The error reported by the step function call
 **/
typedef bool (*rtype_test_op)(uint32_t a, uint32_t b, uint32_t out, bool imm, mips_error error);

/**
 * Required signature for a multiply/divide test operation
 * a : The first operand value
 * b : The second operand value
 * out : The recorded value from the CPU
 * imm : true if the second value is a 16-bit constant
 * error : The error reported by the step function call
 */
typedef bool (*hilo_test_op)(uint32_t a, uint32_t b, uint64_t out, mips_error error);

/** The size of temp_buf **/
#define BUF_SIZE 256
/** A temporary character buffer used to build error messages **/
static char temp_buf[BUF_SIZE];

/** Test for ADD (rtype_test_op) */
bool add_test(uint32_t a, uint32_t b, uint32_t out, bool imm, mips_error error)
{
	int32_t x = a;
	int32_t y = imm ? (uint32_t)(int16_t)b : (uint32_t)b;
	if ((y > 0 && x > INT_MAX - y) ||
		(y < 0 && x < INT_MIN - y))
		return (error == mips_ExceptionArithmeticOverflow);
	return !error && ((int32_t)out == (x + y));
}

/** Test for SUB (rtype_test_op) */
bool sub_test(uint32_t a, uint32_t b, uint32_t out, bool imm, mips_error error)
{
	return add_test(a, -b, out, imm, error);
}

/** Test for ADDU (rtype_test_op) */
bool addu_test(uint32_t a, uint32_t b, uint32_t out, bool imm, mips_error error)
{
	if(imm)
		b = (int16_t)b;
	return !error && (out == (a + b));
}

/** Test for SUBU (rtype_test_op) */
bool subu_test(uint32_t a, uint32_t b, uint32_t out, bool imm, mips_error error)
{
	return !error && (out == (a - b));
}

/** Test for AND (rtype_test_op) */
bool and_test(uint32_t a, uint32_t b, uint32_t out, bool imm, mips_error error)
{
	return !error && (out == (a & b));
}

/** Test for OR (rtype_test_op) */
bool or_test(uint32_t a, uint32_t b, uint32_t out, bool imm, mips_error error)
{
	return !error && (out == (a | b));
}

/** Test for XOR (rtype_test_op) */
bool xor_test(uint32_t a, uint32_t b, uint32_t out, bool imm, mips_error error)
{
	return !error && (out == (a ^ b));
}

/** Test for NOR (rtype_test_op) */
bool nor_test(uint32_t a, uint32_t b, uint32_t out, bool imm, mips_error error)
{
	return !error && (out == ~(a | b));
}

/** Test for SLL (rtype_test_op) */
bool sll_test(uint32_t a, uint32_t b, uint32_t out, bool imm, mips_error error)
{
	return !error && (out == (a << (b & 0x1F)));
}

/** Test for SRL (rtype_test_op) */
bool srl_test(uint32_t a, uint32_t b, uint32_t out, bool imm, mips_error error)
{
	return !error && (out == (a >> (b & 0x1F)));
}

/** Test for SRA (rtype_test_op) */
bool sra_test(uint32_t a, uint32_t b, uint32_t out, bool imm, mips_error error)
{
	return !error && ((int32_t)out == ((int32_t)a >> (b & 0x1F)));
}

 /** Test for SLT (rtype_test_op) */
bool slt_test(uint32_t a, uint32_t b, uint32_t out, bool imm, mips_error error)
{
	if(out != 0 && out != 1)
		return false;
	if(imm)
		b = (int16_t)b;
	return !error && (out ? ((int32_t)a < (int32_t)b) : ((int32_t)a >= (int32_t)b));
}

/** Test for SLTU (rtype_test_op) */
bool sltu_test(uint32_t a, uint32_t b, uint32_t out, bool imm, mips_error error)
{
	if(out != 0 && out != 1)
		return false;
	if(imm)
		b = (int16_t)b;
	return !error && (out ? (a < b) : (a >= b));
}

/** Test for MULT (hilo_test_op) */
bool mult_test(uint32_t a, uint32_t b, uint64_t out, mips_error error)
{
	return !error && ((int64_t)out == (int64_t)(int32_t)a * (int64_t)(int32_t)b);
}

/** Test for MULTU (hilo_test_op) */
bool multu_test(uint32_t a, uint32_t b, uint64_t out, mips_error error)
{
	return !error && (out == (uint64_t)a * (uint64_t)b);
}

/** Test for DIV (hilo_test_op) */
bool div_test(uint32_t a, uint32_t b, uint64_t out, mips_error error)
{
	return !error && ((int32_t)(out >> 32) == (int32_t)a % (int32_t)b) && ((int32_t)out == (int32_t)a / (int32_t)b);
}

/** Test for DIVU (hilo_test_op) */
bool divu_test(uint32_t a, uint32_t b, uint64_t out, mips_error error)
{
	return !error && ((uint32_t)(out >> 32) == a % b) && ((uint32_t)out == a / b);
}

/** The list of R-type test operations, by index */
static const rtype_test_op rtype_tests[13] =
{
	&add_test,
	&addu_test,
	&sub_test,
	&subu_test,

	&and_test,
	&or_test,
	&xor_test,
	&nor_test,

	&sll_test,
	&srl_test,
	&sra_test,

	&slt_test,
	&sltu_test
};

/** The list of multiply/divide operations */
static const hilo_test_op hilo_tests[4] =
{
	&mult_test,
	&multu_test,
	&div_test,
	&divu_test
};

/** The list of I-type (xxxI) operations */
static const rtype_test_op itype_tests[7] =
{
	&add_test,
	&addu_test,
	&and_test,
	&or_test,
	&xor_test,
	&slt_test,
	&sltu_test
};

/** The list of immediate shift operations */
static const rtype_test_op shift_tests[3] =
{
	&sll_test,
	&srl_test,
	&sra_test
};

/** The number of test values */
#define NUM_VALUES 5

/** General edge case values to test with */
static const uint32_t test_values[NUM_VALUES] =
{
	0x00000000,
	0x00000001,
	0x7FFFFFFF,
	0x80000000,
	0xFFFFFFFF
};

/** Slightly less edgier cases for multiply/divide */
static const uint32_t hilo_test_values[NUM_VALUES] =
{
	0x00000001,
	0xFFFFFFFF,
	0x12345678,
	0x87654321,
	0x10000000
};

/** The 16-bit immediate values hardcoded in memory */
static const uint32_t imm_test_values[NUM_VALUES] =
{
	0x0000,
	0x0001,
	0x7FFF,
	0x8000,
	0xFFFF
};

/** The shift immediate values hardcoded in memory */
static const uint32_t shift_test_values[NUM_VALUES] =
{
	0, 1, 2, 3, 4
};

/**
 * The general test for R-type operations (test_op)
 * It runs a test for each possible combination of values,
 * using the index parameter to find an rtype_test_op
 * to see if the answer is correct.
 **/
void rtype_test(const char* name, mips_cpu_h state, mips_mem_h mem, unsigned index)
{
	int i, j;
	uint32_t a, b, out;
	int testID;
	bool pass;
	rtype_test_op test = rtype_tests[index];
	mips_error error;
	for(i = 0; i < NUM_VALUES; i++)
	{
		a = test_values[i];
		for(j = 0; j < NUM_VALUES; j++)
		{
			testID = mips_test_begin_test(name);
			b = test_values[j];
			mips_cpu_set_pc(state, 0);
			mips_cpu_set_register(state, 1, a);
			mips_cpu_set_register(state, 2, b);
			error = mips_cpu_step(state);
			mips_cpu_get_register(state, 3, &out);
			pass = test(a, b, out, false, error);
			if(!pass)
				sprintf(temp_buf, "%d, %d = %d (%s)", a, b, out, mips_error_string(error));
			mips_test_end_test(testID, pass, pass ? NULL : temp_buf);
		}
	}
}
/**
 * The general test for I-type operations
 * name : The instruction name
 * state : The CPU object
 * index : The index parameter, passed straight through
 * values : The value list to use
 * tests : The test operation list to use
 **/
void imm_test_base(const char* name, mips_cpu_h state, unsigned index, const uint32_t* values, const rtype_test_op* tests)
{
	int i, j;
	uint32_t v1, out;
	mips_error error;
	rtype_test_op test = tests[index];
	int testID;
	bool pass;
	for(i = 0; i < NUM_VALUES; i++)
	{
		v1 = test_values[i];
		for(j = 0; j < NUM_VALUES; j++)
		{
			testID = mips_test_begin_test(name);
			mips_cpu_set_pc(state, j << 2);
			mips_cpu_set_register(state, 1, v1);
			error = mips_cpu_step(state);
			mips_cpu_get_register(state, 3, &out);
			pass = test(v1, values[j], out, true, error);
			if(!pass)
				sprintf(temp_buf, "%d, %d = %d (%s)", v1, (int16_t)values[j], out, mips_error_string(error));
			mips_test_end_test(testID, pass, pass ? NULL : temp_buf);
		}
	}
}

/** The test for ADDI, XORI et. al. (test_op) */
void itype_test(const char* name, mips_cpu_h state, mips_mem_h mem, unsigned index)
{
	imm_test_base(name, state, index, imm_test_values, itype_tests);
}

/** The test for SLL et. al. (test_op) */
void shift_test(const char* name, mips_cpu_h state, mips_mem_h mem, unsigned index)
{
	imm_test_base(name, state, index, shift_test_values, shift_tests);
}

/** The test for MULT, DIV, and unsigned variants (test_op) */
void hilo_test(const char* name, mips_cpu_h state, mips_mem_h mem, unsigned index)
{
	int i, j, testID;
	uint32_t v1, v2, o1, o2;
	uint64_t out;
	mips_error error;
	hilo_test_op test = hilo_tests[index];
	bool pass;
	for(i = 0; i < NUM_VALUES; i++)
	{
		v1 = hilo_test_values[i];
		for(j = 1; j < NUM_VALUES; j++)
		{
			testID = mips_test_begin_test(name);
			v2 = hilo_test_values[j];
			mips_cpu_set_pc(state, 0);
			mips_cpu_set_register(state, 1, v1);
			mips_cpu_set_register(state, 2, v2);
			error = mips_cpu_step(state);
			mips_cpu_step(state);
			mips_cpu_step(state);
			mips_cpu_get_register(state, 3, &o1);
			mips_cpu_get_register(state, 4, &o2);
			out = (uint64_t)o1 << 32;
			out |= (uint64_t)o2;
			pass = test(v1, v2, out, error);
			if(!pass)
				sprintf(temp_buf, "%d, %d = %d (%s)", v1, v2, (int32_t)out, mips_error_string(error));
			mips_test_end_test(testID, pass, pass ? NULL : temp_buf);
		}
	}
}

/**
 * Test for LUI (test_op)
 * This test runs a single instruction and compares
 * the destination register with the indexed immediate test value
 **/
void lui_test(const char* name, mips_cpu_h state, mips_mem_h mem, unsigned index)
{
	int i, testID;
	uint32_t v, out;
	mips_error error;
	bool pass;
	for(i = 0; i < NUM_VALUES; i++)
	{
		testID = mips_test_begin_test(name);
		v = imm_test_values[i] << 16;
		mips_cpu_set_pc(state, i << 2);
		error = mips_cpu_step(state);
		mips_cpu_get_register(state, 3, &out);
		pass = !error && (out == v);
		if(!pass)
			sprintf(temp_buf, "%d => %d (%s)", v, out, mips_error_string(error));
		mips_test_end_test(testID, pass, pass ? NULL : temp_buf);
	}
}

/**
 * Base functionality for load operations, LW, LB et. al.
 * This test sets the required registers, runs a single operation
 * and compares the destination register with the given value
 * name : The instruction name
 * state : The CPU object
 * mem : The memory object
 * offset : The address stored in register 1
 * value : The value to compare with
 **/
void load_base(const char* name, mips_cpu_h state, mips_mem_h mem, uint32_t offset, uint32_t value)
{
	int testID = mips_test_begin_test(name);
	bool pass;
	uint32_t out = 0;
	mips_error error;
	mips_cpu_set_pc(state, 0);
	mips_cpu_set_register(state, 1, offset);
	error = mips_cpu_step(state);
	mips_cpu_get_register(state, 3, &out);
	pass = !error && (out == value);
	if(!pass)
		sprintf(temp_buf, "%d => %d (%s)", value, out, mips_error_string(error));
	mips_test_end_test(testID, pass, pass ? NULL : temp_buf);
}

/** Test for LW (test_op) */
void lw_test(const char* name, mips_cpu_h state, mips_mem_h mem, unsigned index)
{
	load_base(name, state, mem, 5, 0x87654321);
}

/** Test for LH (test_op) */
void lh_test(const char* name, mips_cpu_h state, mips_mem_h mem, unsigned index)
{
	load_base(name, state, mem, 5, 0xffff8765);
}

/** Test for LB (test_op) */
void lb_test(const char* name, mips_cpu_h state, mips_mem_h mem, unsigned index)
{
	load_base(name, state, mem, 5, 0xffffff87);
}

/** Test for LHU (test_op) */
void lhu_test(const char* name, mips_cpu_h state, mips_mem_h mem, unsigned index)
{
	load_base(name, state, mem, 5, 0x00008765);
}

/** Test for LBU (test_op) */
void lbu_test(const char* name, mips_cpu_h state, mips_mem_h mem, unsigned index)
{
	load_base(name, state, mem, 5, 0x00000087);
}

/**
 * Base functionality for store instructions (SW, SB et. al.)
 * name : The instruction name
 * state : The CPU object
 * mem : The memory object
 * offset : The address stored in register 1
 * store : The value stored in register 3
 * value : The value to compare the read memory with
 **/
void store_base(const char* name, mips_cpu_h state, mips_mem_h mem, uint32_t offset, uint32_t store, uint32_t value)
{
	int testID = mips_test_begin_test(name);
	uint32_t out = 0;
	bool pass;
	mips_error error;
	mips_cpu_set_pc(state, 0);
	mips_cpu_set_register(state, 1, offset);
	mips_cpu_set_register(state, 3, store);
	error = mips_cpu_step(state);
	mips_mem_read(mem, 4, 4, (uint8_t*)&out);
	reverse_word(&out);
	pass = !error && (out == value);
	if(!pass)
		sprintf(temp_buf, "%d => %d (%s)", value, out, mips_error_string(error));
	mips_test_end_test(testID, pass, pass ? NULL : temp_buf);

}

/** Test for SW (test_op) **/
void sw_test(const char* name, mips_cpu_h state, mips_mem_h mem, unsigned index)
{
	store_base(name, state, mem, 5, 0x12345678, 0x12345678);
}

/** Test for SH (test_op) **/
void sh_test(const char* name, mips_cpu_h state, mips_mem_h mem, unsigned index)
{
	store_base(name, state, mem, 7, 0x12345678, 0x87655678);
}

/** Test for SB (test_op) **/
void sb_test(const char* name, mips_cpu_h state, mips_mem_h mem, unsigned index)
{
	store_base(name, state, mem, 8, 0x12345678, 0x87654378);
}

/** Test for LWL (test_op) **/
void lwl_test(const char* name, mips_cpu_h state, mips_mem_h mem, unsigned index)
{
	mips_cpu_set_register(state, 3, 0x12345678);
	load_base(name, state, mem, 8, 0x789A5678);
}

/** Test for LWR (test_op) **/
void lwr_test(const char* name, mips_cpu_h state, mips_mem_h mem, unsigned index)
{
	mips_cpu_set_register(state, 3, 0x12345678);
	load_base(name, state, mem, 9, 0x1234789A);
}

/** Test for SWL (test_op) **/
void swl_test(const char* name, mips_cpu_h state, mips_mem_h mem, unsigned index)
{
	store_base(name, state, mem, 6, 0x87654321, 0x12876578);
}

/** Test for SWR (test_op) **/
void swr_test(const char* name, mips_cpu_h state, mips_mem_h mem, unsigned index)
{
	store_base(name, state, mem, 7, 0x87654321, 0x12432178);
}

/**
 * Base functionality for branch/jump instructions
 * These tests execute 4 steps, then compare R1 with a test value
 * to see which exact instructions were executed
 * name : The instruction name
 * testName : The name of this specific test
 * state : The CPU object
 * value : The value stored in register 2
 * test : The value to compare with register 1
 * index : The index of the link register, or 0 for no link
 **/
void branch_base(const char* name, const char* testName, mips_cpu_h state, uint32_t value, uint32_t test, unsigned index)
{
	int i;
	mips_error error = 0, last_error = 0;
	uint32_t out, pcn;
	int testID = mips_test_begin_test(name);
	bool pass;
	mips_cpu_set_pc(state, 0);
	mips_cpu_set_register(state, 1, 0);
	mips_cpu_set_register(state, 2, value);
	for(i = 0; i < 4; i++)
	{
		last_error = mips_cpu_step(state);
		error |= last_error;
	}
	mips_cpu_get_register(state, 1, &out);
	if(index)
		mips_cpu_get_register(state, index, &pcn);
	pass = !error && (out == test) && (!index || pcn == 12);
	if(!pass)
	{
		if(index)
			sprintf(temp_buf, "%s $%d = %d [%d] (%s)", testName, index, pcn, 12, mips_error_string(last_error));
		else
			sprintf(temp_buf, "%s (%s)", testName, mips_error_string(last_error));
	}
	mips_test_end_test(testID, pass, pass ? NULL : temp_buf);
}

/** Test for J and JAL (test_op) **/
void jump_test(const char* name, mips_cpu_h state, mips_mem_h mem, unsigned index)
{
	branch_base(name, "Unconditional", state, 0, 0xB, index);
}

/** Test for BEQ (test_op) **/
void beq_test(const char* name, mips_cpu_h state, mips_mem_h mem, unsigned index)
{
	mips_cpu_set_register(state, 3, 0x12345678);
	branch_base(name, "Equal", state, 0x12345678, 0xB, 0);
	branch_base(name, "Not equal", state, 0x87654321, 0x7, 0);
}

/** Test for BNE (test_op) **/
void bne_test(const char* name, mips_cpu_h state, mips_mem_h mem, unsigned index)
{
	mips_cpu_set_register(state, 3, 0x12345678);
	branch_base(name, "Equal", state, 0x12345678, 0x7, 0);
	branch_base(name, "Not equal", state, 0x87654321, 0xB, 0);
}

/** Test for BREAK (test_op) **/
void break_test(const char* name, mips_cpu_h state, mips_mem_h mem, unsigned index)
{
	int testID = mips_test_begin_test(name);
	bool pass;
	mips_error error;
	mips_cpu_set_pc(state, 0);
	error = mips_cpu_step(state);
	pass = (error == mips_ExceptionBreak);
	mips_test_end_test(testID, pass, pass ? NULL : mips_error_string(error));
}

/** Test for SYSCALL (test_op) (test_op) **/
void syscall_test(const char* name, mips_cpu_h state, mips_mem_h mem, unsigned index)
{
	int testID = mips_test_begin_test(name);
	bool pass;
	mips_error error;
	mips_cpu_set_pc(state, 0);
	error = mips_cpu_step(state);
	pass = (error == mips_ExceptionSystemCall);
	mips_test_end_test(testID, pass, pass ? NULL : mips_error_string(error));
}

/**
 * Common settings for branch instructions that compare with zero
 * Whether they pass on less than, greater than, or equal to zero,
 * and whether they link
 **/
typedef struct
{
	bool lt, gt, eq, link;
} bzero_set;

/** The settings for all branch-zero instructions **/
static const bzero_set bzero_sets[6] =
{
	{ true, false, false, false }, /** BLTZ */
	{ false, true, true, false },  /** BGEZ */
	{ true, false, true, false },  /** BLTZ */
	{ false, true, false, false }, /** BGEZ */
	{ true, false, false, true },  /** BLTZAL */
	{ false, true, true, true },   /** BGEZAL */
};

/** The test for BLTZ, BGEZ et. al. **/
void bzero_test(const char* name, mips_cpu_h state, mips_mem_h mem, unsigned index)
{
	bzero_set set = bzero_sets[index];
	branch_base(name, "Less than", state, -1, set.lt ? 0xB : 0x7, set.link ? 31 : 0);
	branch_base(name, "Greater than", state, 1, set.gt ? 0xB : 0x7, set.link ? 31 : 0);
	branch_base(name, "Zero", state, 0, set.eq ? 0xB : 0x7, set.link ? 31 : 0);
}

/** Test for JR and JALR **/
void jr_test(const char* name, mips_cpu_h state, mips_mem_h mem, unsigned index)
{
	branch_base(name, "Unconditional", state, 16, 0xB, 0);
}

/**
 * Base functionality for MF(HI/LO) instructions
 * Since there's no API method to read/write the HI/LO registers,
 * we have to just do a multiplication and check each register
 * name : The instruction name
 * reg : The register name (HI/LO)
 * state : The CPU object
 * test : The value to test against register 3
 **/
void mf_base(const char* name, const char* reg, mips_cpu_h state, uint32_t test)
{
	mips_error error;
	bool pass;
	uint32_t out;
	int testID = mips_test_begin_test(name);
	mips_cpu_set_pc(state, 0);
	mips_cpu_set_register(state, 1, 0x87654321);
	mips_cpu_set_register(state, 2, 2);
	mips_cpu_step(state);
	error = mips_cpu_step(state);
	mips_cpu_get_register(state, 3, &out);
	pass = !error && (out == test);
	if(!pass)
		sprintf(temp_buf, "$%s = 0x%x (%s)", reg, out, mips_error_string(error));
	mips_test_end_test(testID, pass, pass ? NULL : temp_buf);
}

/** Test for MFHI (test_op) **/
void mfhi_test(const char* name, mips_cpu_h state, mips_mem_h mem, unsigned index)
{
	mf_base(name, "HI", state, 0x1);
}

/** Test for MFLO (test_op) **/
void mflo_test(const char* name, mips_cpu_h state, mips_mem_h mem, unsigned index)
{
	mf_base(name, "LO", state, 0xECA8642);
}

/** Information about a single instruction test **/
typedef struct
{
	/** Pointer to the test function itself **/
	test_op test;
	/** Optional index parameter passed to the test function **/
	unsigned index;
	/** The instruction name **/
	const char* name;
	/** Initial data to be written to memory, usually containing the test code **/
	uint32_t data[8];
} test_info;

/** The test data itself **/
static const test_info tests[56] =
{
	{ &rtype_test, 0, "ADD", 	{0x20182200} },
	{ &rtype_test, 1, "ADDU", 	{0x21182200} },
	{ &rtype_test, 2, "SUB", 	{0x22182200} },
	{ &rtype_test, 3, "SUBU", 	{0x23182200} },

	{ &rtype_test, 4, "AND", 	{0x24182200} },
	{ &rtype_test, 5, "OR", 	{0x25182200} },
	{ &rtype_test, 6, "XOR", 	{0x26182200} },
/*	{ &rtype_test, 7, "NOR", 	{0x27182200} },*/

	{ &rtype_test, 8, "SLLV", 	{0x04184100} },
	{ &rtype_test, 9, "SRLV", 	{0x06184100} },
/*	{ &rtype_test,10, "SRAV", 	{0x07184100} },*/

	{ &shift_test, 0, "SLL",	{0x00180100, 0x40180100, 0x80180100, 0xC0180100, 0x00190100} },
	{ &shift_test, 1, "SRL",	{0x02180100, 0x42180100, 0x82180100, 0xC2180100, 0x02190100} },
	{ &shift_test, 2, "SRA",	{0x03180100, 0x43180100, 0x83180100, 0xC3180100, 0x03190100} },

	{ &itype_test, 0, "ADDI", 	{0x00002320, 0x01002320, 0xFF7F2320, 0x00802320, 0xFFFF2320} },
	{ &itype_test, 1, "ADDIU", 	{0x00002324, 0x01002324, 0xFF7F2324, 0x00802324, 0xFFFF2324} },
	{ &itype_test, 2, "ANDI", 	{0x00002330, 0x01002330, 0xFF7F2330, 0x00802330, 0xFFFF2330} },
	{ &itype_test, 3, "ORI", 	{0x00002334, 0x01002334, 0xFF7F2334, 0x00802334, 0xFFFF2334} },
	{ &itype_test, 4, "XORI", 	{0x00002338, 0x01002338, 0xFF7F2338, 0x00802338, 0xFFFF2338} },

	{ &hilo_test,  0, "MULT",	{0x18002200, 0x10180000, 0x12200000} },
	{ &hilo_test,  1, "MULTU",	{0x19002200, 0x10180000, 0x12200000} },
	{ &hilo_test,  2, "DIV",	{0x1A002200, 0x10180000, 0x12200000} },
	{ &hilo_test,  3, "DIVU",	{0x1B002200, 0x10180000, 0x12200000} },

	{ &rtype_test,11, "SLT",	{0x2A182200} },
	{ &rtype_test,12, "SLTU",	{0x2B182200} },
	{ &itype_test, 5, "SLTI",	{0x00002328, 0x01002328, 0xFF7F2328, 0x00802328, 0xFFFF2328} },
	{ &itype_test, 6, "SLTIU",	{0x0000232C, 0x0100232C, 0xFF7F232C, 0x0080232C, 0xFFFF232C} },

	{ &lui_test,   0, "LUI",	{0x0000033C, 0x0100033C, 0xFF7F033C, 0x0080033C, 0xFFFF033C} },

	{ &lw_test,    0, "LW",		{0xFFFF238C, 0x21436587} },
/*	{ &lh_test,    0, "LH",		{0xFFFF2384, 0x21436587} },*/
	{ &lb_test,    0, "LB",		{0xFFFF2380, 0x21436587} },
/*	{ &lhu_test,   0, "LHU",	{0xFFFF2394, 0x21436587} },*/
	{ &lbu_test,   0, "LBU",	{0xFFFF2390, 0x21436587} },

	{ &sw_test,    0, "SW",		{0xFFFF23AC, 0x21436587} },
	{ &sh_test,    0, "SH",		{0xFFFF23A4, 0x21436587} },
	{ &sb_test,    0, "SB",		{0xFFFF23A0, 0x21436587} },

	{ &lwl_test,   0, "LWL",	{0xFFFF2388, 0x78563412, 0xF0DEBC9A} },
	{ &lwr_test,   0, "LWR",	{0xFFFF2398, 0x78563412, 0xF0DEBC9A} },
/*	{ &swl_test,   0, "SWL",	{0xFFFF23A8, 0x78563412, 0xF0DEBC9A} },*/
/*	{ &swr_test,   0, "SWR",	{0xFFFF23B8, 0x78563412, 0xF0DEBC9A} },*/

	{ &jump_test,  0, "J",		{0x01002134, 0x04000008, 0x02002134, 0x04002134, 0x08002134} },
	{ &jump_test, 31, "JAL",	{0x01002134, 0x0400000C, 0x02002134, 0x04002134, 0x08002134} },
	{ &jr_test,   31, "JR",		{0x01002134, 0x08004000, 0x02002134, 0x04002134, 0x08002134} },
/*	{ &jr_test,    3, "JALR",	{0x01002134, 0x09184000, 0x02002134, 0x04002134, 0x08002134} },*/

	{ &beq_test,   0, "BEQ",	{0x01002134, 0x02004310, 0x02002134, 0x04002134, 0x08002134} },
	{ &bne_test,   0, "BNE",	{0x01002134, 0x02004314, 0x02002134, 0x04002134, 0x08002134} },
	{ &bzero_test, 0, "BLTZ",	{0x01002134, 0x02004004, 0x02002134, 0x04002134, 0x08002134} },
	{ &bzero_test, 1, "BGEZ",	{0x01002134, 0x02004104, 0x02002134, 0x04002134, 0x08002134} },
	{ &bzero_test, 2, "BLEZ",	{0x01002134, 0x02004018, 0x02002134, 0x04002134, 0x08002134} },
	{ &bzero_test, 3, "BGTZ",	{0x01002134, 0x0200401C, 0x02002134, 0x04002134, 0x08002134} },
	{ &bzero_test, 4, "BLTZAL",	{0x01002134, 0x02005004, 0x02002134, 0x04002134, 0x08002134} },
	{ &bzero_test, 5, "BGEZAL",	{0x01002134, 0x02005104, 0x02002134, 0x04002134, 0x08002134} },

/*	{ &break_test, 0, "BREAK",	{0x0D000000} },*/
/*	{ &syscall_test,0,"SYSCALL",{0x0C000000} },*/

	{ &mfhi_test,  0, "MFHI",	{0x19002200, 0x10180000} },
	{ &mflo_test,  0, "MFLO",	{0x19002200, 0x12180000} },

};

void do_test(mips_cpu_h state, mips_mem_h mem, unsigned index)
{
	test_info info = tests[index];
	mips_mem_write(mem, 0,
					sizeof(info.data), (uint8_t*)info.data);
	if(info.test != NULL)
		info.test(info.name, state, mem, info.index);
}

int main()
{
	mips_mem_h mem = mips_mem_create_ram(64, 4);
	mips_cpu_h cpu = mips_cpu_create(mem);
	unsigned i;
	mips_test_begin_suite();
	for(i = 0; i < 52; i++)
		do_test(cpu, mem, i);
	mips_test_end_suite();
	mips_cpu_free(cpu);
	mips_mem_free(mem);
	return 0;
}

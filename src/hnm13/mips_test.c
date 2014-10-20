#include "mips_test.h"
#include "mips_cpu.h"
#include "limits.h"
#include <stdbool.h>

typedef bool (*test_op)(mips_cpu_h state, unsigned index);
typedef bool (*rtype_test_op)(uint32_t a, uint32_t b, uint32_t out, bool imm, mips_error error);

bool add_test(uint32_t a, uint32_t b, uint32_t out, bool imm, mips_error error)
{
	int32_t x = a;
	int32_t y = imm ? (int16_t)b : b;
	if ((y > 0 && x > INT_MAX - y) ||
		(y < 0 && x < INT_MIN - y))
		return (error == mips_ExceptionArithmeticOverflow);
	//printf("ADDTEST: %x == %x + %x (%d)\n", out, a, b, imm);
	return !error && (out == (x + y));
}

bool sub_test(uint32_t a, uint32_t b, uint32_t out, bool imm, mips_error error)
{
	return add_test(a, -b, out, imm, error);
}

bool addu_test(uint32_t a, uint32_t b, uint32_t out, bool imm, mips_error error)
{
	if(imm)
		b = (int16_t)b;
	return !error && (out == (a + b));
}

bool subu_test(uint32_t a, uint32_t b, uint32_t out, bool imm, mips_error error)
{
	return !error && (out == (a - b));
}

bool and_test(uint32_t a, uint32_t b, uint32_t out, bool imm, mips_error error)
{
	return !error && (out == (a & b));
}

bool or_test(uint32_t a, uint32_t b, uint32_t out, bool imm, mips_error error)
{
	return !error && (out == (a | b));
}

bool xor_test(uint32_t a, uint32_t b, uint32_t out, bool imm, mips_error error)
{
	return !error && (out == (a ^ b));
}

bool nor_test(uint32_t a, uint32_t b, uint32_t out, bool imm, mips_error error)
{
	return !error && (out == ~(a | b));
}

static const rtype_test_op rtype_tests[8] =
{
	&add_test,
	&addu_test,
	&sub_test,
	&subu_test,

	&and_test,
	&or_test,
	&xor_test,
	&nor_test
};

static const rtype_test_op itype_tests[5] =
{
	&add_test,
	&addu_test,
	&and_test,
	&or_test,
	&xor_test
};

#define NUM_VALUES 5
static const uint32_t test_values[NUM_VALUES] =
{
	0x00000000,
	0x00000001,
	0x7FFFFFFF,
	0x80000000,
	0xFFFFFFFF
};

static const uint16_t imm_test_values[NUM_VALUES] =
{
	0x0000,
	0x0001,
	0x7FFF,
	0x8000,
	0xFFFF
};

bool rtype_single_test(mips_cpu_h state, uint32_t a, uint32_t b, rtype_test_op test)
{
	//printf("TEST: %x %x\n", a, b);
	mips_cpu_set_pc(state, 0);
	mips_cpu_set_register(state, 1, a);
	mips_cpu_set_register(state, 2, b);
	mips_error error = mips_cpu_step(state);
	uint32_t out;
	mips_cpu_get_register(state, 3, &out);
	//printf("RESULT: %d\n", test(a, b, out ,error));
	return test(a, b, out, false, error);
}

bool rtype_test(mips_cpu_h state, unsigned index)
{
	int i, j;
	bool pass = true;
	for(i = 0; i < NUM_VALUES; i++)
		for(j = 0; j < NUM_VALUES; j++)
			pass &= rtype_single_test(state, test_values[i], test_values[j], rtype_tests[index]);
	return pass;
}

bool itype_test(mips_cpu_h state, unsigned index)
{
	int i, j;
	bool pass = true;
	uint32_t v1, out;
	mips_error error;
	rtype_test_op test = itype_tests[index - 8];
	for(i = 0; i < NUM_VALUES; i++)
	{
		v1 = test_values[i];
		for(j = 0; j < NUM_VALUES; j++)
		{
			mips_cpu_set_pc(state, j << 2);
			mips_cpu_set_register(state, 1, v1);
			error = mips_cpu_step(state);
			mips_cpu_get_register(state, 3, &out);
			pass &= test(v1, (uint16_t)imm_test_values[j] & 0xFFFF, out, true, error);
			//printf("PASS: %d %x %x %x\n", pass, v1, (uint16_t)imm_test_values[j], out);
		}
	}
	return pass;
}

typedef struct
{
	test_op test;
	const char* name;
	uint32_t data[8];
} test_info;

static const test_info tests[56] =
{
	{ &rtype_test, "ADD", {0x20182200} },
	{ &rtype_test, "ADDU", {0x21182200} },

	{ &rtype_test, "SUB", {0x22182200} },
	{ &rtype_test, "SUBU", {0x23182200} },

	{ &rtype_test, "AND", {0x24182200} },
	{ &rtype_test, "OR", {0x25182200} },
	{ &rtype_test, "XOR", {0x26182200} },
	{ &rtype_test, "NOR", {0x27182200} },

	{ &itype_test, "ADDI", {0x00002320, 0x01002320, 0xFF7F2320, 0x00802320, 0xFFFF2320} },
	{ &itype_test, "ADDIU", {0x00002324, 0x01002324, 0xFF7F2324, 0x00802324, 0xFFFF2324} },
	{ &itype_test, "ANDI", {0x00002330, 0x01002330, 0xFF7F2330, 0x00802330, 0xFFFF2330} },
	{ &itype_test, "ORI", {0x00002334, 0x01002334, 0xFF7F2334, 0x00802334, 0xFFFF2334} },
	{ &itype_test, "XORI", {0x00002338, 0x01002338, 0xFF7F2338, 0x00802338, 0xFFFF2338} },
};

void do_test(mips_cpu_h state, mips_mem_h mem, unsigned index)
{
	test_info info = tests[index];
	printf("Doing test %s\n", info.name);
	mips_mem_write(mem, 0,
					sizeof(info.data), (uint8_t*)info.data);
	printf("%d\n", info.test(state, index));
}

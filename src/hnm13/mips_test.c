#include "mips_test.h"
#include "mips_cpu.h"
#include "limits.h"
#include <stdbool.h>

typedef bool (*test_op)(mips_cpu_h state, unsigned index);
typedef bool (*rtype_test_op)(uint32_t a, uint32_t b, uint32_t out, bool imm, mips_error error);
typedef bool (*hilo_test_op)(uint32_t a, uint32_t b, uint64_t out, mips_error error);

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

bool sll_test(uint32_t a, uint32_t b, uint32_t out, bool imm, mips_error error)
{
	return !error && (out == (a << (b & 0x1F)));
}

bool srl_test(uint32_t a, uint32_t b, uint32_t out, bool imm, mips_error error)
{
	return !error && (out == (a >> (b & 0x1F)));
}

bool sra_test(uint32_t a, uint32_t b, uint32_t out, bool imm, mips_error error)
{
	return !error && (out == ((int32_t)a >> (b & 0x1F)));
}

bool slt_test(uint32_t a, uint32_t b, uint32_t out, bool imm, mips_error error)
{
	if(out != 0 && out != 1)
		return false;
	if(imm)
		b = (int16_t)b;
	return !error && (out ? ((int32_t)a < (int32_t)b) : ((int32_t)a >= (int32_t)b));
}

bool sltu_test(uint32_t a, uint32_t b, uint32_t out, bool imm, mips_error error)
{
	if(out != 0 && out != 1)
		return false;
	if(imm)
		b = (int16_t)b;
	return !error && (out ? (a < b) : (a >= b));
}

bool mult_test(uint32_t a, uint32_t b, uint64_t out, mips_error error)
{
	return !error && (out == (int64_t)(int32_t)a * (int64_t)(int32_t)b);
}

bool multu_test(uint32_t a, uint32_t b, uint64_t out, mips_error error)
{
	return !error && (out == (uint64_t)a * (uint64_t)b);
}

bool div_test(uint32_t a, uint32_t b, uint64_t out, mips_error error)
{
	return !error && ((int32_t)(out >> 32) == (int32_t)a % (int32_t)b) && ((int32_t)out == (int32_t)a / (int32_t)b);
}

bool divu_test(uint32_t a, uint32_t b, uint64_t out, mips_error error)
{
	return !error && ((uint32_t)(out >> 32) == a % b) && ((uint32_t)out == a / b);
}

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

static const hilo_test_op hilo_tests[4] =
{
	&mult_test,
	&multu_test,
	&div_test,
	&divu_test
};

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

static const rtype_test_op shift_tests[3] =
{
	&sll_test,
	&srl_test,
	&sra_test
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

static const uint32_t hilo_test_values[NUM_VALUES] =
{
	0x00000001,
	0xFFFFFFFF,
	0x12345678,
	0x87654321,
	0x10000000
};

static const uint32_t imm_test_values[NUM_VALUES] =
{
	0x0000,
	0x0001,
	0x7FFF,
	0x8000,
	0xFFFF
};

static const uint32_t shift_test_values[NUM_VALUES] =
{
	0, 1, 2, 3, 4
};

bool rtype_test(mips_cpu_h state, unsigned index)
{
	int i, j;
	bool pass = true;
	uint32_t a, b, out;
	rtype_test_op test = rtype_tests[index];
	for(i = 0; i < NUM_VALUES; i++)
	{
		a = test_values[i];
		for(j = 0; j < NUM_VALUES; j++)
		{
			b = test_values[j];
			mips_cpu_set_pc(state, 0);
			mips_cpu_set_register(state, 1, a);
			mips_cpu_set_register(state, 2, b);
			mips_error error = mips_cpu_step(state);
			mips_cpu_get_register(state, 3, &out);
			pass &= test(a, b, out, false, error);
		}
	}
	return pass;
}

bool imm_test_base(mips_cpu_h state, unsigned index, const uint32_t* values, const rtype_test_op* tests)
{
	int i, j;
	bool pass = true;
	uint32_t v1, out;
	mips_error error;
	rtype_test_op test = tests[index];
	for(i = 0; i < NUM_VALUES; i++)
	{
		v1 = test_values[i];
		for(j = 0; j < NUM_VALUES; j++)
		{
			mips_cpu_set_pc(state, j << 2);
			mips_cpu_set_register(state, 1, v1);
			error = mips_cpu_step(state);
			mips_cpu_get_register(state, 3, &out);
			pass &= test(v1, values[j], out, true, error);
		}
	}
	return pass;
}

bool itype_test(mips_cpu_h state, unsigned index)
{
	return imm_test_base(state, index, imm_test_values, itype_tests);
}

bool shift_test(mips_cpu_h state, unsigned index)
{
	return imm_test_base(state, index, shift_test_values, shift_tests);
}

bool hilo_test(mips_cpu_h state, unsigned index)
{
	int i, j;
	uint32_t v1, v2, o1, o2;
	uint64_t out;
	mips_error error;
	hilo_test_op test = hilo_tests[index];
	bool p, pass = true;
	for(i = 0; i < NUM_VALUES; i++)
	{
		v1 = hilo_test_values[i];
		for(j = 1; j < NUM_VALUES; j++)
		{
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
			p = test(v1, v2, out, error);
			pass &= p;
			if(!p)
				printf("FAIL\n");
		}
	}
	return pass;
}

bool lui_test(mips_cpu_h state, unsigned index)
{
	int i;
	uint32_t v, out;
	mips_error error;
	bool pass = true;
	for(i = 0; i < NUM_VALUES; i++)
	{
		v = imm_test_values[i] << 16;
		mips_cpu_set_pc(state, i << 2);
		error = mips_cpu_step(state);
		mips_cpu_get_register(state, 3, &out);
		pass &= (out == v);
	}
	return pass;
}

typedef struct
{
	test_op test;
	unsigned index;
	const char* name;
	uint32_t data[8];
} test_info;

static const test_info tests[56] =
{
	{ &rtype_test, 0, "ADD", 	{0x20182200} },
	{ &rtype_test, 1, "ADDU", 	{0x21182200} },
	{ &rtype_test, 2, "SUB", 	{0x22182200} },
	{ &rtype_test, 3, "SUBU", 	{0x23182200} },

	{ &rtype_test, 4, "AND", 	{0x24182200} },
	{ &rtype_test, 5, "OR", 	{0x25182200} },
	{ &rtype_test, 6, "XOR", 	{0x26182200} },
	{ &rtype_test, 7, "NOR", 	{0x27182200} },

	{ &rtype_test, 8, "SLLV", 	{0x04184100} },
	{ &rtype_test, 9, "SRLV", 	{0x06184100} },
	{ &rtype_test,10, "SRAV", 	{0x07184100} },

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

	{ &lw_test,    0, "LW",		{} },
	{ &lh_test,    0, "LH",		{} },
	{ &lb_test,    0, "LB",		{} },
	{ &lhu_test,   0, "LHU",	{} },
	{ &lbu_test,   0, "LBU",	{} },

	{ &sw_test,    0, "SW",		{} },
	{ &sh_test,    0, "SH",		{} },
	{ &sb_test,    0, "SB",		{} },

	{ &lwl_test,   0, "LWL",	{} },
	{ &lwr_test,   0, "LWR",	{} },
	{ &swl_test,   0, "SWL",	{} },
	{ &swr_test,   0, "SWR",	{} },

};

void do_test(mips_cpu_h state, mips_mem_h mem, unsigned index)
{
	test_info info = tests[index];
	printf("Doing test %s\n", info.name);
	mips_mem_write(mem, 0,
					sizeof(info.data), (uint8_t*)info.data);
	printf("%d\n", info.test(state, info.index));
	getchar();
}
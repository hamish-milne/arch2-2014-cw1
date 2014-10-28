#ifndef mips_cpu_extend_header
#define mips_cpu_extend_header

#include "mips_util.h"

mips_error mips_cpu_set_coprocessor(mips_cpu_h state,
	unsigned index,
	coprocessor cp);

mips_error mips_cpu_set_debug_level(mips_cpu_h state,
	unsigned level,
	FILE *dest);

mips_error mips_cpu_set_exception_handler(mips_cpu_h state,
	mips_error exception,
	uint32_t handler);

#endif // mips_cpu_extend_header

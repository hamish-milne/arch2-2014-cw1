#include "mips_core.h"
#include <stdint.h>

#ifndef mips_util_header
#define mips_util_header

/** Signature for a general operation
 *  'state' can be assumed valid */
typedef mips_error (*op)(mips_cpu_h state, uint32_t instruction);

/** Signature for an I-type operation
 *  'state' can be assumed valid */
typedef mips_error (*cop_load_store)(mips_cpu_h state, unsigned reg, uint32_t* data);

typedef void (*debug_handle)(mips_cpu_h state, const char* message, size_t len);

/** Struct for a set of coprocessor functions  */
typedef struct
{
	op cop;
	cop_load_store lwc, swc;
} coprocessor;

extern const mips_error mips_ExceptionCoprocessorUnusable;
extern const mips_error mips_ExceptionSystemCall;

extern const char* mips_error_string(mips_error error);

extern void reverse_word(uint32_t* word);

extern mips_error mips_cpu_set_debug_handler(mips_cpu_h state, debug_handle handle);

extern mips_error mips_cpu_set_coprocessor(mips_cpu_h state, unsigned index, coprocessor cp);

#endif

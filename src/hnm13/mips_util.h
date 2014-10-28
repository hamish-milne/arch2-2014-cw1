#ifndef mips_util_header
#define mips_util_header

#include "mips_mem.h"
#include <stdint.h>

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

static const mips_error mips_ExceptionCoprocessorUnusable = mips_InternalError + 1;
static const mips_error mips_ExceptionSystemCall = mips_InternalError + 2;

static const char* errors[16] =
{
	"Not implemented",
	"Invalid argument",
	"Invalid handle",
	"File read error",
	"File write error",
	0,0,0,
	0,0,0,0,
	0,0,0,0
};

static const char* exceptions[16] =
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

static const char* mips_error_string(mips_error error)
{
	unsigned code = error >> 16;
	const char* ret = NULL;
	if(code == 1)
		ret = errors[error & 0xF];
	else if(code == 2)
		ret = exceptions[error & 0xF];
	if(ret == NULL)
		ret = "Unhandled exception";
	return ret;
}

/** Reverses the byte order of the given input */
static void reverse_word(uint32_t* word)
{
	uint32_t ret;
	uint8_t *iptr = (uint8_t*)word + 4;
	uint8_t *optr = (uint8_t*)&ret;
	uint8_t *end = optr + 4;
	while(optr < end)
		*optr++ = *--iptr;
	*word = ret;
}

static mips_error mips_load_file(mips_mem_h mem, const char* file)
{
	unsigned len;
	mips_error error;
	uint8_t* temp;
	FILE* fp = fopen(file, "rb");
	if(fp == NULL || ferror(fp))
		return mips_ErrorFileReadError;
	fseek(fp, 0, SEEK_END);
	len = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	temp = malloc(len);
	fread(temp, 1, len, fp);
	error = mips_ErrorFileReadError;
	if(!ferror(fp))
		error = mips_mem_write(mem, 0, len, temp);

	free(temp);
	fclose(fp);
	return error;
}

#endif

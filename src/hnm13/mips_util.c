#include "mips_core.h"

const mips_error mips_ExceptionCoprocessorUnusable = 0x2006;
const mips_error mips_ExceptionSystemCall = 0x2007;

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

const char* mips_error_string(mips_error error)
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

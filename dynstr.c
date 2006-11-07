/*
 * dynstr.c
 * 
 * Copyright (C) 2004 PDC, KTH. See COPYING for license details.
 * 
 * Dynamic string. This is not at all UTF8-secure. :/
*/

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "mem.h"
#include "strutil.h"

#include "dynstr.h"

/* ----------------------------------------------------------------------------------------- */

#define	APPEND_MARGIN	32

struct DynStr
{
	char	*str;
	size_t	len;		/* Length of string, in characters not including terminator. */
	size_t	alloc;		/* Allocated bytes. Often more than len, when appending. */
};

/* ----------------------------------------------------------------------------------------- */

DynStr * dynstr_new(const char *text)
{
	DynStr	*ds;

	if((ds = mem_alloc(sizeof *ds)) != NULL)
	{
		if(text == NULL)
		{
			ds->str = NULL;
			ds->alloc = ds->len = 0;
			return ds;
		}
		else if((ds->str = stu_strdup(text)) != NULL)
		{
			ds->len   = strlen(ds->str);
			ds->alloc = ds->len;
			return ds;
		}
		mem_free(ds);
	}
	return NULL;
}

DynStr * dynstr_new_sized(size_t size)
{
	DynStr	*ds;

	if(size == 0)
		return NULL;
	if((ds = mem_alloc(sizeof *ds)) != NULL)
	{
		if((ds->str = mem_alloc(size + 1)) != NULL)
		{
			ds->str[0] = '\0';	/* Make sure string is properly terminated, and empty. */
			ds->len    = 0;
			ds->alloc  = size;
			return ds;
		}
		mem_free(ds);
	}
	return NULL;
}

DynStr * dynstr_new_from_file(const char *filename)
{
	DynStr	*ds = NULL;
	FILE	*in;

	if(filename == NULL)
		return NULL;
	if((in = fopen(filename, "rb")) != NULL)
	{
		if(fseek(in, 0, SEEK_END) == 0)
		{
			long	size;

			size = ftell(in);
			if(size > 0 && fseek(in, 0, SEEK_SET) == 0)
			{
				ds = dynstr_new_sized(size);
				if(ds != NULL)
				{
					if(fread(ds->str, size, 1, in) == 1)
					{
						ds->len = size;
						ds->str[size] = '\0';
					}
				}
			}
		}
		fclose(in);
	}
	return ds;
}

void dynstr_assign(DynStr *str, const char *text)
{
	size_t	len;
	char	*nb;

	if(str == NULL || text == NULL)
		return;
	len = strlen(text);
	if(len <= str->len && len < 128)
	{
		strcpy(str->str, text);
		str->len = len;
		return;
	}
	if((nb = mem_alloc(len + 1)) != NULL)
	{
		mem_free(str->str);
		str->str   = nb;
		str->alloc = len + 1;
		str->len   = len;
		strcpy(str->str, text);
	}
	else
		LOG_WARN(("Couldn't assign string, out of memory"));
}

/* Compute size needed for string formatting. A portability thing. If run on Linux
 * with a pre-2.0 C runtime, this will fail horribly.
*/
static size_t format_size(const char *fmt, va_list args)
{
#if defined _WIN32
	return _vscprintf(fmt, args);
#else
	char	buf;
	int	n;

	n = vsnprintf(&buf, 1, fmt, args);
	if(n >= 0)	/* Very quick on modern glibc runtimes. */
		return n;
	else
	{
		LOG_ERR(("Negative return handler for vsnprintf(), not implemented"));
	}
#endif
	LOG_ERR(("Failing, falling through to return 0"));
	return 0;
}

void dynstr_printf(DynStr *str, const char *fmt, ...)
{
	va_list	args;
	size_t	need;
	char	*nb;

	if(str == NULL || fmt == NULL)
		return;

	va_start(args, fmt);
	need = format_size(fmt, args);
	va_end(args);

	if(need == 0)	/* Size computation failed, or string is really boring. */
		return;
	if(need + 1 < str->alloc)
	{
		str->len = vsprintf(str->str, fmt, args);
		return;
	}
	if((nb = mem_realloc(str->str, need + 1)) != NULL)
	{
		str->str = nb;
		vsprintf(str->str, fmt, args);
		str->len = need;
		str->alloc = need + 1;
	}
}

void dynstr_append(DynStr *str, const char *text)
{
	size_t	len;

	if(str == NULL || text == NULL || *text == '\0')
		return;
	len = strlen(text);
	dynstr_append_len(str, text, len);
}

void dynstr_append_len(DynStr *str, const char *text, size_t len)
{
	char	*nb;

	if(str == NULL || text == NULL || len == 0)
		return;
	if(str->len + len + 1 <= str->alloc)
	{
		memcpy(str->str + str->len, text, len);
		str->len += len;
		str->str[str->len] = '\0';
		return;
	}
	if((nb = mem_realloc(str->str, str->alloc + len + 1 + APPEND_MARGIN)) != NULL)
	{
		str->str = nb;
		memcpy(str->str + str->len, text, len);
		str->len += len;
		str->str[str->len] = '\0';
		str->alloc = str->alloc + len + 1 + APPEND_MARGIN;
/*		LOG_MSG(("String grew to %u bytes, with %u allocated", str->len, str->alloc));*/
		return;
	}
	LOG_ERR(("Couldn't allocate new buffer for append"));
}

void dynstr_append_c(DynStr *str, char c)
{
	if(str == NULL || c == '\0')
		return;
	if(str->alloc > str->len + 1)
	{
		str->str[str->len++] = c;
		str->str[str->len] = '\0';
	}
	else
		dynstr_append_len(str, &c, 1);
}

void dynstr_append_printf(DynStr *str, const char *fmt, ...)
{
	va_list	args;
	int	need;

	if(str == NULL || fmt == NULL || *fmt == '\0')
		return;

	va_start(args, fmt);
	need = format_size(fmt, args);
	va_end(args);
	if(need == 0)
		return;
	else if(need <= (int) (str->alloc - str->len - 1))
	{
		va_start(args, fmt);
		vsprintf(str->str + str->len, fmt, args);
		va_end(args);
		str->len += need;
		return;
	}
	else
	{
		char	*nb;

		if((nb = mem_realloc(str->str, str->alloc + need + 1 + APPEND_MARGIN)) != NULL)
		{
			str->str = nb;
			va_start(args, fmt);
			vsprintf(str->str + str->len, fmt, args);
			va_end(args);
			str->len   += need;
			str->alloc += need + 1 + APPEND_MARGIN;
			return;
		}
	}
	LOG_ERR(("Couldn't allocate new buffer for append printf"));
}

const char * dynstr_string(const DynStr *str)
{
	if(str == NULL)
		return NULL;
	return str->str;
}

size_t dynstr_length(const DynStr *str)
{
	if(str == NULL)
		return 0;
	return str->len;
}

void dynstr_trim(DynStr *str)
{
	unsigned int	i, trim;

	if(str == NULL)
		return;
	for(i = trim = 0; i < str->len && isspace(str->str[i]); i++)
		trim++;
	if(trim > 0)
	{
		memmove(str->str, str->str + trim, str->len - trim);
		str->len -= trim;
		str->str[str->len] = '\0';
	}
	for(i = str->len, trim = 0; i > 0 && isspace(str->str[i - 1]); i--)
		trim++;
	if(trim > 0)
	{
		str->len -= trim;
		str->str[str->len] = '\0';
	}
}

void dynstr_truncate(DynStr *str, size_t size)
{
	if(str == NULL)
		return;
	if(size >= str->len)
		return;
	str->len = size;
	str->str[str->len] = '\0';
}

char * dynstr_destroy(DynStr *str, int destroy_buffer)
{
	char	*ret = NULL;

	if(str == NULL)
		return NULL;
	if(!destroy_buffer)
		ret = str->str;
	else
		mem_free(str->str);
	mem_free(str);

	return ret;
}

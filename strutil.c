/*
 * strutil.c
 * 
 * Copyright (C) 2004 PDC, KTH. See COPYING for license details.
 * 
 * String utility functions. Prefixed by stu_ since str is reserved by C.
*/

#include <stdlib.h>
#include <string.h>

#include "mem.h"

#include "strutil.h"

char * stu_strdup(const char *str)
{
	if(str != NULL)
	{
		char	*n;
		size_t	len = strlen(str);

		if((n = mem_alloc(len + 1)) != NULL)
		{
			strcpy(n, str);
			return n;
		}
	}
	return NULL;
}

char * stu_strdup_maxlen(const char *str, size_t max)
{
	if(str != NULL && max > 0)
	{
		char	*n;
		size_t	len = strlen(str) + 1, nl;

		nl = len > max ? max : len;
		if((n = mem_alloc(nl)) != NULL)
			stu_strncpy(n, nl, str);
		return n;
	}
	return NULL;
}

char * stu_strncpy(char *dest, size_t max, const char *src)
{
	char	*base = dest;
	
	if(dest == NULL || src == NULL)
		return NULL;
	if(max == 0)
		return NULL;
	for(max--; max > 0 && *src != '\0'; max--)
		*dest++ = *src++;
	*dest = '\0';

	return base;
}

char * stu_strncpy_accept_null(char *dest, size_t max, const char *src)
{
	if(src == NULL)
	{
		if(max > 0)
			*dest = '\0';
		return dest;
	}
	return stu_strncpy(dest, max, src);
}

char ** stu_split(const char *string, char split)
{
	const char	*p;
	char		**str, *put;
	size_t		len = 0;
	int		num = 1, i;

	if(string == NULL || *string == '\0')
		return NULL;
	/* Strip off any initial split chars, simplifies things. */
	while(*string == split)
		string++;

	/* Make an initial pass, computing number of parts and their total length. */
	for(p = string; *p; p++)
	{
		if(*p == split)
		{
			for(++p; *p == split; p++)
				;
			if(*p)
				num++;
			p--;
		}
		if(*p != '\\')
			len++;
	}
	len++;		/* Add one for (final) terminator. */
	str = mem_alloc((num + 1) * sizeof *str + len);

	/* Go through string again, setting pointers and copying characters. */
	put = (char *) str + ((num + 1) * sizeof *str);
	for(i = 0, p = string; i < num; i++)
	{
		str[i] = put;
		while(*p != '\0' && *p != split)
			*put++ = *p++;
		*put++ = '\0';
		while(*p == split)
			p++;
	}
	/* Terminate pointer array. */
	str[i] = NULL;

	return str;
}

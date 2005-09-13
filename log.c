/*
 * log.c
 * 
 * Copyright (C) 2004 PDC, KTH. See COPYING for license details.
 * 
 * Logging primitives. Please use macros in log.h in actual code, the functions themselves
 * are a bit rough around the edges for technical reasons. It can probably be done better.
*/

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "log.h"

/* ----------------------------------------------------------------------------------------- */

static struct {
	enum LogLevel	level;
	const char	*file;
	int		line;
} log_info;

/* ----------------------------------------------------------------------------------------- */

static void do_log_full(enum LogLevel lvl, const char *file, int line, const char *fmt, va_list arg)
{
	const char	*type = "";

	switch(lvl)
	{
	case LOG_MESSAGE:
		type = "--Message";
		break;
	case LOG_WARNING:
		type = "++Warning";
		break;
	case LOG_ERROR:
		type = "**Error";
		break;
	}
	if(file != NULL)
		printf("%s [%s:%d] ", type, file, line);
	vprintf(fmt, arg);
	putchar('\n');
}

/* ----------------------------------------------------------------------------------------- */

void log_setup(enum LogLevel lvl, const char *file, int line)
{
	log_info.level = lvl;
	log_info.file  = file;
	log_info.line  = line;
}

void log_finalize(const char *fmt, ...)
{
	va_list	arg;

	va_start(arg, fmt);
	do_log_full(log_info.level, log_info.file, log_info.line, fmt, arg);
	va_end(arg);
}

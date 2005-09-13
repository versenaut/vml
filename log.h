/*
 * log.h
 * 
 * Copyright (C) 2004 PDC, KTH. See COPYING for license details.
 * 
 * Logging support.
*/

/* Macros to use to emit messages, warnings, and errors. Nevermind the right-hand side.
 *
 * Usage: LOG_MSG((format, ...)); -- like printf() with format and args. Mind the extra
 * parens and don't terminate format with newline.
 * 
*/
#define	LOG_MSG(L)	do { log_setup(LOG_MESSAGE, __FILE__, __LINE__); log_finalize L; } while(0)
#define	LOG_WARN(L)	do { log_setup(LOG_WARNING, __FILE__, __LINE__); log_finalize L; } while(0)
#define	LOG_ERR(L)	do { log_setup(LOG_ERROR,   __FILE__, __LINE__); log_finalize L; } while(0)

/* Below are implementation-specifics, best ignored. */

enum LogLevel { LOG_MESSAGE, LOG_WARNING, LOG_ERROR };

extern void	log_setup(enum LogLevel lvl, const char *file, int line);
extern void	log_finalize(const char *fmt, ...);

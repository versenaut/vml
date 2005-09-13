/*
 * mem.c
 * 
 * Copyright (C) 2004 PDC, KTH. See COPYING for license details.
 * 
 * 
*/

#include <stdio.h>
#include <stdlib.h>

#include "log.h"
#include "mem.h"

static enum MemMode the_mode = MEM_NULL_ERROR;

static int report = 0;

void mem_mode_set(enum MemMode mode)
{
	if(mode < 0 || mode >= 3)
	{
		LOG_WARN(("Can't set mode %d, not defined", mode));
		return;
	}
	the_mode = mode;
}

void mem_report_set(int r)
{
	report = r;
}

static void * do_return(const char *src, void *p, size_t size, const void *old)
{
	if(report)
	{
		if(old != NULL)
			LOG_MSG(("Did %s of %u, got %p (old %p)", src, size, p, old));
		else
			LOG_MSG(("Did %s of %u, got %p", src, size, p));
	}
	if(p != NULL)
		return p;
	switch(the_mode)
	{
	case MEM_NULL_RETURN:
		return NULL;
	case MEM_NULL_WARN:
		LOG_WARN(("Failed to %s %u bytes, returning NULL", src, size));
		return NULL;
	case MEM_NULL_ERROR:
		LOG_ERR(("Failed to %s %u bytes, aborting", src, size));
		abort();
	}
	return NULL;
}

void * mem_alloc(size_t size)
{
	return do_return("malloc", malloc(size), size, NULL);
}

void * mem_realloc(void *ptr, size_t size)
{
	return do_return("realloc", realloc(ptr, size), size, ptr);
}

void mem_free(void *ptr)
{
	if(report)
		LOG_MSG(("Freeing memory at %p", ptr));
	if(ptr != NULL)
		free(ptr);
}

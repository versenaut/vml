/*
 * mem.h
 * 
 * Copyright (C) 2004 PDC, KTH. See COPYING for license details.
 * 
 * Memory handling functions, to ease debugging and stuff in the future.
 *
 * Note that not all allocations go through these, there are various modules
 * that use underlying system APIs directly and no attempt have been made
 * to trap their allocations (for instance dynlib).
*/

#if !defined MEM_H
#define	MEM_H

enum MemMode { MEM_NULL_RETURN, MEM_NULL_WARN, MEM_NULL_ERROR };

/* What should happen when an allocation fails? */
extern void	mem_mode_set(enum MemMode mode);

extern void	mem_report_set(int r);

/* Allocate <size> bytes of fresh storage. */
extern void *	mem_alloc(size_t size);

/* Reallocate buffer at <ptr>, growing it to <size> bytes. */
extern void *	mem_realloc(void *ptr, size_t size);

/* Free allocation at <ptr>, allowing memory to be re-used. */
extern void	mem_free(void *ptr);

#endif		/* MEM_H */

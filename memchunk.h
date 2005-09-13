/*
 * memchunk.h
 * 
 * Copyright (C) 2004 PDC, KTH. See COPYING for license details.
 * 
 * A "chunked" memory allocation system, to make allocation of many small
 * blocks a bit more efficient. Very much inspired by glib's API. Does not
 * shrink, from an external perspective, until destroyed.
 *
 * The overhead per true allocation is currently 2 * sizeof (void *), i.e. 8
 * bytes on most 32-bit systems. If chunk_size = 12 and growth = 16, this
 * means that 256 bytes will be used for every 16 allocations.
*/

#include <stdlib.h>

typedef struct MemChunk	MemChunk;

extern MemChunk *	memchunk_new(const char *name, size_t chunk_size, size_t growth);
extern size_t		memchunk_chunk_size(const MemChunk *chunk);
extern void *		memchunk_alloc(MemChunk *chunk);
extern void		memchunk_free(MemChunk *chunk, void *ptr);
extern void		memchunk_destroy(MemChunk *chunk);

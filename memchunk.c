/*
 * memchunk.c
 * 
 * Copyright (C) 2004 PDC, KTH. See COPYING for license details.
 * 
 * Chunked allocation utility module. Makes allocating and freeing many small
 * blocks of the same size into an O(1) activity, on average.
*/

#include <stdlib.h>

#include "log.h"
#include "mem.h"
#include "strutil.h"

#include "memchunk.h"

/* ----------------------------------------------------------------------------------------- */

typedef struct Block	Block;

struct Block
{
	Block	*next;		/* Next actual block, to be returned to user. */
	Block	*next_alloc;	/* Next chunk of blocks, as allocated. For freeing. */
	/* User's data block begins here. */
};

struct MemChunk
{
	char	name[32];
	size_t	size;
	size_t	growth;

	Block	*next;
	Block	*next_alloc;
	size_t	num_alloc;
};

/* ----------------------------------------------------------------------------------------- */

/* We're fresh out of chunks, so allocate more. */
static void grow(MemChunk *chunk)
{
	void	*buf;

	if((buf = mem_alloc(chunk->growth * (chunk->size + sizeof (Block)))) != NULL)
	{
		Block	*here = buf, *first = here;
		size_t	i;

/*		LOG_MSG(("Memchunk \"%s\" grew", chunk->name));*/

		here->next = chunk->next;
		for(i = 0; i < chunk->growth - 1; i++, here = (Block *) (here->next))
			here->next = (Block *) ((char *) here + sizeof *here + chunk->size);
		here->next = chunk->next;
		chunk->next = first;

		first->next_alloc = chunk->next_alloc;
		chunk->next_alloc = first;
		chunk->num_alloc++;
	}
}

MemChunk * memchunk_new(const char *name, size_t chunk_size, size_t growth)
{
	MemChunk	*c;

	if(name == NULL || chunk_size < 4 || growth < 4)
	{
		LOG_ERR(("Unsupported parameters to memchunk_new()"));
		return NULL;
	}

	c = mem_alloc(sizeof *c);
	stu_strncpy(c->name, sizeof c->name, name);
	c->size   = chunk_size;
	c->growth = growth;
	c->next = NULL;
	c->next_alloc = NULL;
	c->num_alloc = 0;

	return c;
}

size_t memchunk_chunk_size(const MemChunk *chunk)
{
	return chunk != NULL ? chunk->size : 0;
}

size_t memchunk_growth(const MemChunk *chunk)
{
	return chunk != NULL ? chunk->growth : 0;
}

void * memchunk_alloc(MemChunk *chunk)
{
	Block	*b;

	if(chunk == NULL)
		return NULL;
	if(chunk->next == NULL)
		grow(chunk);
	if(chunk->next == NULL)
		return NULL;
	b = chunk->next;
	chunk->next = b->next;
	b->next = NULL;
	return (char *) b + sizeof *b;
}

void memchunk_free(MemChunk *chunk, void *ptr)
{
	Block	*b = (Block *) ((char *) ptr - sizeof *b);

	b->next = chunk->next;
	chunk->next = b;
}

void memchunk_destroy(MemChunk *chunk)
{
	if(chunk != NULL)
	{
		Block	*here, *next;

		for(here = chunk->next_alloc; here != NULL; here = next)
		{
			next = here->next_alloc;
			mem_free(here);
		}
		mem_free(chunk);
	}
}

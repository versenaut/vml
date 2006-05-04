/*
 * list.c
 * 
 * Copyright (C) 2004 PDC, KTH. See COPYING for license details.
 * 
 * A run-of-the-mill doubly linked list.
*/

#include "memchunk.h"

#include "list.h"

/* ----------------------------------------------------------------------------------------- */

struct List
{
	List	*prev, *next;
	void	*data;
};

static MemChunk	*the_chunk = NULL;

/* ----------------------------------------------------------------------------------------- */

void list_init(void)
{
	the_chunk = memchunk_new("List", sizeof (List), 64);
}

/* ----------------------------------------------------------------------------------------- */

List * list_new(void *data)
{
	List	*list;

	if((list = memchunk_alloc(the_chunk)) != NULL)
	{
		list->prev = list->next = NULL;
		list->data = data;
	}
	return list;
}

List * list_append(List *list, void *data)
{
	List	*tail;

	if((tail = list_new(data)) != NULL)
	{
		if(list != NULL)
		{
			List	*iter;

			for(iter = list; iter->next != NULL; iter = iter->next)
				;
			iter->next = tail;
			tail->prev = iter;
			return list;
		}
		return tail;
	}
	return NULL;
}

List * list_prepend(List *list, void *data)
{
	List	*head;

	if((head = list_new(data)) != NULL)
	{
		if(list != NULL)
		{
			list->prev = head;
			head->next = list;
		}
		return head;
	}
	return NULL;
}

List * list_insert_before(List *list, List *parent, void *data)
{
	List	*el, *prev;

	if(list == NULL)
		return list_new(data);
	if(parent == NULL)
		return list_append(list, data);
	el = list_new(data);
	if((prev = list_prev(parent)) != NULL)
		prev->next = el;
	el->prev = prev;
	el->next = parent;
	parent->prev = el;
	if(list == parent)
		return el;
	return list;
}

List * list_insert_sorted(List *list, void *data, int (*compare)(const void *data1, const void *data2))
{
	List	*iter;

	if(list == NULL || compare == NULL)
		return list_new(data);
	for(iter = list; iter != NULL; iter = list_next(iter))
	{
		if(compare(data, list_data(iter)) < 0)
			return list_insert_before(list, iter, data);
	}
	return list_append(list, data);
}

List * list_concat(List *head, List *tail)
{
	if(head != NULL)
	{
		List	*iter;
	
		for(iter = head; iter->next != NULL; iter = iter->next)
			;
		iter->next = tail;
		if(tail != NULL)
			tail->prev = iter;
		return head;
	}
	if(tail != NULL)
	{
		tail->prev = NULL;
		return tail;
	}
	return NULL;
}

List * list_reverse(List *list)
{
	List	*iter, *prev, *next;

	if(list == NULL)
		return NULL;
	for(iter = list; iter->next != NULL; iter = iter->next)
		;
	list = iter;
	for(prev = NULL, next = NULL; iter != NULL; prev = iter, iter = next)
	{
		next = iter->prev;
		iter->next = next;
		iter->prev = prev;
	}
	return list;
}

List * list_remove(List *list, void *data)
{
	List	*iter;

	for(iter = list; iter != NULL; iter = list_next(iter))
	{
		if(list_data(iter) == data)
		{
			list = list_unlink(list, iter);
			list_destroy(iter);
			return list;
		}
	}
	return list;
}

List * list_unlink(List *list, List *element)
{
	List	*prev, *next;

	if(list == NULL)
		return NULL;
	if(element == NULL)
		return list;
	prev = element->prev;
	next = element->next;
	if(prev != NULL)
		prev->next = next;
	if(next != NULL)
		next->prev = prev;
	element->prev = element->next = NULL;

	if(list == element)
		return prev ? prev : next;

	return list;
}

List * list_split(List *list, List *element)
{
	if(list == NULL)
		return NULL;
	if(element == NULL)
		return list;
	if(element->prev == NULL)
		return NULL;
	if(list->next == NULL)
		return NULL;
	element->prev->next = NULL;
	element->prev = NULL;
	return list;
}

List * list_tail(List *list)
{
	List	*head;

	if(list == NULL)
		return NULL;
	head = list;
	list = list_unlink(list, head);
	list_destroy(head);

	return list;
}

void * list_data(const List *list)
{
	if(list != NULL)
		return list->data;
	return NULL;
}

void list_data_set(List *list, void *data)
{
	if(list != NULL)
		list->data = data;
}

List * list_prev(const List *list)
{
	if(list != NULL)
		return list->prev;
	return NULL;
}

List * list_next(const List *list)
{
	if(list != NULL)
		return list->next;
	return NULL;
}

size_t list_length(const List *list)
{
	size_t	len = 0;

	for(; list != NULL; len++, list = list->next)
		;

	return len;
}

List * list_nth(List *list, size_t n)
{
	for(; list != NULL; list = list->next, n--)
	{
		if(n == 0)
			return list;
	}
	return NULL;
}

size_t list_index(List *list)
{
	size_t	index = 0;

	for(; list != NULL && list->prev != NULL; index++, list = list->prev)
		;

	return index;
}

List * list_first(List *list)
{
	if(list == NULL)
		return NULL;
	for(; list->prev != NULL; list = list->prev)
		;
	return list;
}

List * list_last(List *list)
{
	if(list == NULL)
		return NULL;
	for(; list->next != NULL; list = list->next)
		;
	return list;
}

void list_foreach(const List *list, int (*callback)(void *data, void *userdata), void *userdata)
{
	for(; list != NULL; list = list->next)
	{
		if(!callback(list->data, userdata))
			break;
	}
}

List * list_find_custom(const List *list, const void *data, int (*compare)(const void *listdata, const void *data))
{
	for(; list != NULL; list = list_next(list))
	{
		if(compare(list_data(list), data) == 0)
			return (List *) list;
	}
	return NULL;
}

List * list_find_sorted(const List *list, const void *data, int (*compare)(const void *listdata, const void *data))
{
	for(; list != NULL; list = list->next)
	{
		int	rel = compare(list_data(list), data);

		if(rel == 0)
			return (List *) list;
		if(rel > 0)		/* Reached element that must be after the sought one? Then fail. */
			return NULL;
	}
	return NULL;
}

void list_destroy(List *list)
{
	List	*next;

	if(list == NULL)
		return;
	if(list->prev != NULL)
		list->prev = NULL;
	for(; list != NULL; list = next)
	{
		next = list->next;
		memchunk_free(the_chunk, list);
	}
}

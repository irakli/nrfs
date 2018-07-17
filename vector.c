#include "vector.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <search.h>

#define DEFAULT_ALLOCATION_SIZE 4

void vector_new(vector *vector, int elem_size, free_fn free, int initial_size)
{
	vector->elem_size = elem_size;
	vector->free = free;

	if (initial_size <= 0)
		vector->initial_allocation = DEFAULT_ALLOCATION_SIZE;
	else
		vector->initial_allocation = initial_size;

	vector->allocated_length = vector->initial_allocation;
	vector->logical_length = 0;
	vector->elems = malloc(vector->elem_size * vector->allocated_length);
}

void vector_dispose(vector *vector)
{
	if (vector->free != NULL)
	{
		size_t i;
		for (i = 0; i < vector->logical_length; i++)
			vector->free((char *)vector->elems + vector->elem_size * i);
	}

	free(vector->elems);
}

size_t vector_length(const vector *vector)
{
	return vector->logical_length;
}

void *vector_nth(const vector *vector, int position)
{
	assert(position >= 0 && position < vector->logical_length);
	return (char *)vector->elems + vector->elem_size * position;
}

void *vector_last(const vector *vector)
{
	return vector_nth(vector, vector_length(vector) - 1);
}

void vector_replace(vector *vector, const void *elem, int position)
{
	assert(position >= 0 && position < vector->logical_length);

	void *ptr = (char *)vector->elems + vector->elem_size * position;
	if (vector->free != NULL)
		vector->free(ptr);
	memcpy(ptr, elem, vector->elem_size);
}

void vector_insert(vector *vector, const void *elem, int position)
{
	assert(position >= 0 && position <= vector->logical_length);

	if (vector->logical_length == vector->allocated_length)
		grow(vector);

	void *src = (char *)vector->elems + vector->elem_size * position;
	void *dest = (char *)src + vector->elem_size;
	memmove(dest, src, (vector->logical_length - position) * vector->elem_size);
	memcpy(src, elem, vector->elem_size);
	vector->logical_length++;
}

void vector_append(vector *vector, const void *elem)
{
	if (vector->logical_length == vector->allocated_length)
		grow(vector);

	void *ptr = (char *)vector->elems + vector->elem_size * vector->logical_length++;
	memcpy(ptr, elem, vector->elem_size);
}

void vector_delete(vector *vector, int position)
{
	assert(position >= 0 && position < vector->logical_length);

	void *ptr = (char *)vector->elems + vector->elem_size * position;
	if (vector->free != NULL)
		vector->free(ptr);
	memmove(ptr, (char *)ptr + vector->elem_size, vector->elem_size * (vector->logical_length-- - 1 - position));
}

void vector_sort(vector *vector, cmp_fn compare)
{
	assert(compare != NULL);

	qsort(vector->elems, vector->logical_length, vector->elem_size, compare);
}

void vector_map(vector *vector, map_fn map, void *aux)
{
	assert(map != NULL);

	size_t i;
	for (i = 0; i < vector->logical_length; i++)
		map((char *)vector->elems + vector->elem_size * i, aux);
}

static void grow(vector *vector)
{
	vector->allocated_length += vector->initial_allocation;
	vector->elems = realloc(vector->elems, vector->allocated_length * vector->elem_size);
}

#define NOT_FOUND -1
int vector_search(const vector *vector, const void *key, cmp_fn search, int start_index, int is_sorted)
{
	assert(start_index >= 0 && start_index <= vector->logical_length);
	assert(search != NULL);

	void *ptr;
	void *base = (char *)vector->elems + start_index * vector->elem_size;
	int result = NOT_FOUND;
	size_t nmemb = vector->logical_length - start_index;

	if (is_sorted)
		ptr = bsearch(key, base, nmemb, vector->elem_size, search);
	else
		ptr = lfind(key, base, &nmemb, vector->elem_size, search);

	if (ptr != NULL)
		result = ((char *)ptr - (char *)vector->elems) / vector->elem_size;
	return result;
}

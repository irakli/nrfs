#ifndef _vector_
#define _vector_

// #include "bool.h"
#include <stdio.h>
#include <stdlib.h>

typedef int (*cmp_fn)(const void *elema, const void *elemb);

typedef void (*map_fn)(void *elem, void *aux_data);

typedef void (*free_fn)(void *elem);

typedef struct
{
	void *elems;
	size_t elem_size;
	size_t allocated_length;
	size_t logical_length;
	size_t initial_allocation;
	void (*free)(void *);
} vector;

void vector_new(vector *vector, int elem_size, free_fn free, int initial_allocation);

void vector_dispose(vector *vector);

size_t vector_length(const vector *vector);

void *vector_nth(const vector *vector, int position);

void *vector_last(const vector *vector);

void vector_insert(vector *vector, const void *elem, int position);

void vector_append(vector *vector, const void *elem);

void vector_replace(vector *vector, const void *elem, int position);

void vector_delete(vector *vector, int position);

int vector_search(const vector *vector, const void *key, cmp_fn search, int start_index, int is_sorted);

void vector_sort(vector *vector, cmp_fn cmp);

void vector_map(vector *vector, map_fn map, void *aux_data);

static void grow(vector *vector);

#endif

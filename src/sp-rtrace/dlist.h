/*
 * This file is copied as it is from sp-rtrace package. It's a part
 * of double linked list support. Consider to replace it with more
 * common data container implementation (glib).
 *
 * Copyright (C) 2010 by Nokia Corporation
 *
 * Contact: Eero Tamminen <eero.tamminen@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02r10-1301 USA
 */

#ifndef DLIST_H
#define DLIST_H

#include "data_ops.h"

/**
 * @file dlist.h
 *
 * Double linked list implementation.
 *
 * This list can be used only with data structures having
 * dlist_node_t structure as the first member.
 * Also this list doesn't manage the resources used by its
 * nodes - the user is responsible for allocating/freeing
 * them.
 * An item example:
 * struct {
 *     dlist_node_t node; // dlist support
 *
 *     char* author;      // user data
 *     char* title;
 * } book_t;
 */

/**
 * Double linked list node.
 *
 * This structure must be declared as the first member for all
 * structures stored in dlist. It contains links to next and
 * previous list items.
 */
typedef struct dlist_node_t {
	struct dlist_node_t* next;
	struct dlist_node_t* prev;
} dlist_node_t;

/**
 * Double linked list.
 *
 */
typedef struct dlist_t {
	/* the last (newest) item in the list */
	dlist_node_t* head;
	/* the first (oldest) item in the list */
	dlist_node_t* tail;
} dlist_t;


/**
 * Reference node for storing pointers into
 * dlist_t list.
 */
typedef struct ref_node_t {
    dlist_node_t node;
    void* ref;
} ref_node_t;

#define REF_NODE(x) 	((ref_node_t*)x)

/* 'hide' internal data representation by providing first/last macros */
#define dlist_first(list)     ((list)->tail)
#define dlist_last(list)      ((list)->head)

/**
 * Initializes the list container.
 *
 * @return
 */
void dlist_init(dlist_t* list);

/**
 * Initializes an array of list containers.
 *
 * This function was added for hash table optimization purposes.
 * The double linked list is used as hash table bucket container
 * which means that hash table must initialize an array of lists
 * during its creation.
 * Depending on implementation calling dlist_init_array can be
 * much faster than calling dlist_init for each array item.
 * @param[in] lists   an array of list containers.
 * @param[in] size    the array size (number of items).
 * @return
 */
void dlist_init_array(dlist_t* lists, int size);

/**
 * Frees the list.
 *
 * If free function is specified it's called for each list
 * node to release resources used by the nodes themselves.
 * @param[in] list       the list to free.
 * @param[in] free_node  an optional node freeing function.
 *                       Can be NULL.
 * @return
 */
void dlist_free(dlist_t* list, op_unary_t free_node);

/**
 * Creates a new node and initializes it.
 *
 * This function aborts execution if allocation failed.
 * @param[in] size   the node size.
 * @return           the created node.
 */
void* dlist_create_node(int size);

/**
 * Adds a new node at the end of list.
 *
 * @param[in] list   the list.
 * @param[in] data   the data to add.
 * @return
 */
void dlist_add(dlist_t* list, void* data);


/**
 * Adds a new node to the list at the place specified by
 * the compare function.
 *
 * This function is used to build sorted lists.
 * @param[in] list      the list.
 * @param[in] data      the data to add.
 * @param[in] compare   the comparison function.
 * @return
 */
void dlist_add_sorted(dlist_t* list, void* data, op_binary_t compare);


/**
 * Adds a new node to the list at the place specified by
 * the compare function.
 *
 * This function is similar to dlist_add_sorted() function, with
 * the exception that the index lookup is performed from the end
 * instead of the beginning.
 * @param[in] list      the list.
 * @param[in] data      the data to add.
 * @param[in] compare   the comparison function.
 * @return
 */
void dlist_add_sorted_r(dlist_t* list, void* data, op_binary_t compare);

/**
 * Finds node containing the specified data.
 *
 * This function iterates through list and returns
 * first item for which compare(item) returns 0.
 * @param[in] list     the list.
 * @param[in] data     the data to match.
 * @param[in] compare  the list node comparison function.
 *                     The default (pointer comparison) is used if
 *                     compare is NULL.
 * @return             The found node or NULL if
 *                     no node matches the specified criteria.
 */
void* dlist_find(dlist_t* list, void* data, op_binary_t compare);


/**
 * Removes node from the list.
 *
 * Note that the node itself is not freed - the user is responsible for
 * node allocation/freeing.
 * @param[in] list   the list.
 * @param[in] node   the node to remove.
 * @return
 */
void dlist_remove(dlist_t* list, void* node);


/**
 * Calls a function for all nodes in the list.
 *
 * @param[in] list     the list.
 * @param[in] do_what  the function to call.
 * @return
 */
void dlist_foreach(dlist_t* list, op_unary_t do_what);

/**
 * Calls a function for all nodes in the list,
 * passing the specified data.
 *
 * @param[in] list     the list.
 * @param[in] do_what  the function to call.
 * @param[in] data     the user data to pass the do_what function.
 * @return
 */
void dlist_foreach2(dlist_t* list, op_binary_t do_what, void* data);

/**
 * Calls a function for all nodes in the defined range.
 *
 * This function calls @p do_what function for all nodes starting with @p from
 * and while the @p do_while(node) returns non zero value.
 * @param[in] list       the list.
 * @param[in] from       the starting node.
 * @param[in] do_while   the range specified.
 * @param[in] do_what    the function to call.
 * @return               the next unprocessed node.
 */
dlist_node_t* dlist_foreach_in(dlist_t* list, dlist_node_t* from, op_unary_t do_while, op_unary_t do_what);

/**
 * Calls the specified function for all nodes in the defined range.
 *
 * This function is similar to dlist_foreach_in function with addition of
 * custom data passed to @p do_* functions.
 * @param[in] list       the list.
 * @param[in] from       the starting node.
 * @param[in] do_while   the range specified.
 * @param[in] data_while the custom data to pass do_while function.
 * @param[in] do_what    the function to call.
 * @param[in] data_what  the custom data to pass do_what function.
 * @return               the next unprocessed node.
 */
dlist_node_t* dlist_foreach2_in(dlist_t* list, dlist_node_t* from,
		op_binary_t do_while, void* data_while, op_binary_t do_what, void* data_what);


/**
 * Sorts the list by the specified comparison function.
 *
 * The sorting is based on merge sort algorithm.
 * @param[in] list      the list.
 * @param[in] compare   the comparison function.
 *
 */
void dlist_sort(dlist_t* list, op_binary_t compare);

#endif

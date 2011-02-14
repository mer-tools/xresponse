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

#include "config.h"

#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <memory.h>
#include <stdbool.h>

#include "dlist.h"
#include "utils.h"

void dlist_init(dlist_t* list)
{
	list->head = NULL;
	list->tail = NULL;
}

void dlist_init_array(dlist_t* lists, int size)
{
	memset(lists, 0, size * sizeof(dlist_t));
}

void dlist_free(dlist_t* list, op_unary_t free_node)
{
    if (free_node) {
        dlist_foreach(list, free_node);
    }
    dlist_init(list);
}

void* dlist_create_node(int size)
{
    dlist_node_t* node = (dlist_node_t*)malloc_a(size);
    /* theoretically next, prev values will be initialized when the node
     * is added to list - so we could left them uninitialized.
     */
    node->next = NULL;
    node->prev = NULL;
    return node;
}

void dlist_add(dlist_t* list, void* node)
{
    assert(node);
	dlist_node_t* list_node = (dlist_node_t*)node;
    if (list->head) {
    	/* append after head item */
        list->head->next = list_node;
    }
    else {
    	/* empty list, set as tail item */
        list->tail = list_node;
    }
    list_node->prev = list->head;
    list_node->next = NULL;
    list->head = list_node;
}

void dlist_add_sorted(dlist_t* list, void* node, op_binary_t compare)
{
	assert(node);
	dlist_node_t* list_node = (dlist_node_t*)node;
	/* First check if the node must be inserted at the beginning of the list
	 * It should be done either when list is empty or when the new member is
	 * less than the first.
	 */
	if (!list->tail || compare(list_node, list->tail) < 0) {
		list_node->next = list->tail;
		list_node->prev = NULL;
		/* If the list was empty initialize it's head reference, otherwise
		 * set the new node as previous node for the first node in the list.
		 */
		if (!list->head) list->head = list_node;
		else list->tail->prev = list_node;
		/* After all links are set, put the new node at the beginning
		 * of the list.
		 */
		list->tail = list_node;
	}
	else {
		dlist_node_t* cur_node = list->tail;
		/* Find the node after which the new node must be inserted */
		while (cur_node->next && compare(list_node, cur_node->next) > 0) {
			cur_node = cur_node->next;
		}
		/* Update list head if the node must be inserted at the end of the list,
		 * otherwise update the previous node link for the next node.
		 */
		if (!cur_node->next) list->head = list_node;
		else cur_node->next->prev = list_node;
		/* Update links for the new node and the node after which it's inserted */
		list_node->next = cur_node->next;
		cur_node->next = list_node;
		list_node->prev = cur_node;
	}
}

void dlist_add_sorted_r(dlist_t* list, void* node, op_binary_t compare)
{
	assert(node);
	dlist_node_t* list_node = (dlist_node_t*)node;
	/* First check if the node must be inserted at the end of the list
	 * It should be done either when list is empty or when the new member is
	 * less than the first.
	 */
	if (!list->head || compare(list_node, list->head) >= 0) {
		list_node->prev = list->head;
		list_node->next = NULL;
		/* If the list was empty initialize it's tail reference, otherwise
		 * set the new node as previous node for the first node in the list.
		 */
		if (!list->tail) list->tail = list_node;
		else list->head->next = list_node;
		/* After all links are set, put the new node at the end
		 * of the list.
		 */
		list->head = list_node;
	}
	else {
		dlist_node_t* cur_node = list->head;
		/* Find the node after which the new node must be inserted */
		while (cur_node->prev && compare(list_node, cur_node->prev) < 0) {
			cur_node = cur_node->prev;
		}
		/* Update list tail if the node must be inserted at the beginning of the list,
		 * otherwise update the next node link for the previous node.
		 */
		if (!cur_node->prev) list->tail = list_node;
		else cur_node->prev->next = list_node;
		/* Update links for the new node and the node after which it's inserted */
		list_node->prev = cur_node->prev;
		cur_node->prev = list_node;
		list_node->next = cur_node;
	}
}

/**
 * Simple pointer comparison.
 *
 * This is the default comparison method for dlist_find function.
 * @param[in] data1
 * @param[in] data2
 * @return
 */
static long compare_pointers(void* data1, void* data2)
{
	return data1 - data2;
}


void* dlist_find(dlist_t* list, void* data, op_binary_t compare)
{
	if (!compare) compare = compare_pointers;
	dlist_node_t* node = dlist_first(list);
	while (node && compare(node, data)) {
		node = node->next;
	}
	return node;
}

void dlist_remove(dlist_t* list, void* node)
{
    assert(node);
	dlist_node_t* list_node = (dlist_node_t*)node;
	if (list_node->prev) list_node->prev->next = list_node->next;
	else list->tail = list_node->next;
	if (list_node->next) list_node->next->prev = list_node->prev;
	else list->head = list_node->prev;
}

void dlist_foreach(dlist_t* list, op_unary_t do_what)
{
	dlist_node_t* node = dlist_first(list);
	while (node) {
	    dlist_node_t* current = node;
	    node = node->next;
	    do_what(current);
	}
}

void dlist_foreach2(dlist_t* list, op_binary_t do_what, void* data)
{
	dlist_node_t* node = dlist_first(list);
	while (node) {
        dlist_node_t* current = node;
        node = node->next;
        do_what(current, data);
	}
}

dlist_node_t* dlist_foreach_in(dlist_t* list, dlist_node_t* from, op_unary_t do_while, op_unary_t do_what)
{
	if (from == NULL) {
		from = dlist_first(list);
	}
	while (from && do_while(from)) {
	    dlist_node_t* node = from;
	    from = from->next;
	    do_what(node);
	}
	return from;
}


dlist_node_t* dlist_foreach2_in(dlist_t* list, dlist_node_t* from,
		op_binary_t do_while, void* data_while, op_binary_t do_what, void* data_what)
{
	if (from == NULL) {
		from = dlist_first(list);
	}
	while (from && do_while(from, data_while)) {
	    dlist_node_t* node = from;
	    from = from->next;
	    do_what(node, data_what);
	}
	return from;
}

/**
 * List segment structure.
 * 
 */
typedef struct {
    dlist_node_t* head;
    dlist_node_t* tail;
} segment_t;

/**
 * Merge two segments.
 * 
 * Two sorted segments are merged so that the resulting segment is already sorted.
 */
static void merge_segments(segment_t* segment1, segment_t* segment2, op_binary_t compare)
{
    dlist_node_t* node1 = segment1->tail;
    dlist_node_t* node2 = segment2->tail;

    /* concatenate segments: segment1+segment2 */
    segment1->head->next = segment2->tail;
    segment2->tail->prev = segment1->head;

    while (true) {
    	/* find the next node in segment1 that is greater than the segment2:current node */
        while (compare(node1, node2) <= 0) {
            if (node1 == segment1->head) {
            	/* if segment1 runs out of nodes, the merging is complete */
                node1->next = node2;
                node2->prev = node1;
                return;
            }
            node1 = node1->next;
        }
        /* insert the segment2:current node before the segment1:current node */
        if (node1 != segment1->tail) node1->prev->next = node2;
        node2->prev = node1->prev;
        /* find the next node in segment 2 that is greater than the segment1:current node */
        while (compare(node2, node1) < 0) {
            if (node2 == segment2->head) {
            	/* if segment2 runs out of nodes, the merging is complete */
                node1->prev = node2;
                node2->next = node1;
                return;
            }
            node2 = node2->next;
        }
        /* insert the segment2:current->prev node before segment1:current node */
        node1->prev = node2->prev;
        node1->prev->next = node1;
    }
}

/**
 * Sorts the segment by the specified comparison function.
 *
 * @param[in] segment   the segment to sort.
 * @param[in] compare   the comparison function.
 */
static void dlist_sort_segment(segment_t* segment, op_binary_t compare)
{
    /* First check primitive cases */
    /* segment contains single item - so it's already sorted */
    if (segment->tail == segment->head) return;
    /* segment contains two items - swap if necessary */
    if (segment->tail->next == segment->head) {
        if (compare(segment->tail, segment->head) > 0) {
            segment->head = segment->tail;
            segment->tail = segment->head->next;
            segment->tail->next = segment->head;
            segment->head->prev = segment->tail;
        }
        return;
    }
    /* segment contains 3+ items - proceed with recursive sorting */
    dlist_node_t* node1 = segment->tail;
    dlist_node_t* node2 = segment->tail;

    /* find the middle node */
    while (node2 != segment->head) {
        node1 = node1->next;
        node2 = node2->next;
        if (node2 == segment->head) break;
        node2 = node2->next;
    }

    /* split into two segments */
    segment_t segment1 = {.tail = segment->tail, .head = node1->prev};
    segment_t segment2 = {.tail = node1, .head = segment->head};
    /* sort the new segments */
    dlist_sort_segment(&segment1, compare);
    dlist_sort_segment(&segment2, compare);

    /* set the new tail/head nodes for the segment */
    segment->tail = compare(segment1.tail, segment2.tail) <= 0 ? segment1.tail : segment2.tail;
    segment->head = compare(segment1.head, segment2.head) > 0 ? segment1.head : segment2.head;

    /* merge the segments back */
    merge_segments(&segment1, &segment2, compare);
}

void dlist_sort(dlist_t* list, op_binary_t compare)
{
    /* list contains one or no records, no sorting necessary */
    if (list->head == list->tail) return;

    segment_t segment = {.head = list->head, .tail = list->tail};
    dlist_sort_segment(&segment, compare);

    /* set the new tail/head nodes for the list */
    list->head = segment.head;
    list->head->next = NULL;
    list->tail = segment.tail;
    list->tail->prev = NULL;
}

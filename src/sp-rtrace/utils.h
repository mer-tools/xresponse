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

#ifndef UTILS_H
#define UTILS_H

#include <stdlib.h>
#include <string.h>

/**
 * @file utils.h
 *
 * Contains utility functions and definitions.
 */


#define MSG_ERROR_CONST(text)  if (write(STDERR_FILENO, text, sizeof(text))) {};

/**
 * Creates new log file path.
 *
 * If dir/pattern path does not exist, it is returned as output path.
 * returned. Otherwise the first not existing dir/filename-<index> is
 * returned (index ranging from 2 to 100).
 * @param[in] dir       the output directory path.
 * @param[in] pattern   the basic log file name.
 * @param[out] path     the name for new log file
 * @param[in] size      the output buffer size.
 * @return              0 - success
 *                      -EINVAL - failed to find log file name.
 */
int get_log_filename(int pid, const char* dir, const char* pattern, char* path, size_t size);

/**
 * Exits with appropriate error message.
 *
 * This function is used to abort execution when memory allocation failure
 * occurs.
 * @param[in] size       the number of bytes attempted to allocate.
 * @return
 */
int exit_on_no_memory(int size);

/* memory allocation macro with Abort on failure */
#define malloc_a(size)         (malloc(size) ? : (exit_on_no_memory(size), NULL))

#define realloc_a(ptr, size)   (realloc(ptr, size) ? : (exit_on_no_memory(size), NULL))

#define calloc_a(count, size)  (calloc(count, size) ? : (exit_on_no_memory((count) * (size)), NULL))

#define strdup_a(str)          (strdup(str) ? : (exit_on_no_memory(-1), NULL))


/* atomic op support */

#ifdef USE_LAOPS

#include <atomic_ops.h>

#define sync_fetch_and_add(addr, value)             AO_fetch_and_add(addr, value)
#define sync_bool_compare_and_swap(addr, is, set)   AO_compare_and_swap(addr, is, set)

#define sync_entity_t		volatile AO_t

#else

#define sync_fetch_and_add(addr, value)             __sync_fetch_and_add(addr, value)
#define sync_bool_compare_and_swap(addr, is, set)   __sync_bool_compare_and_swap(addr, is, set)

#define sync_entity_t	 	volatile int

#endif // USE_AOPS

/* checks if the application is running in scratchbox */
#define query_scratchbox() (access("/targets/links/scratchbox.config", F_OK) == 0)

#endif

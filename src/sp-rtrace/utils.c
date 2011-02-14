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

#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <execinfo.h>

#include "utils.h"

#define MAX_INDEX     100

int get_log_filename(int pid, const char* dir, const char* pattern, char* path, size_t size)
{
	int index = 0;
	do {
		int offset = snprintf(path, size, "%s/", dir);
		snprintf(path + offset, size - offset, pattern, pid, index++);
	} while (access(path, F_OK) == 0);
	return 0;
}

int exit_on_no_memory(int size)
{
	fprintf(stderr, "ERROR: not enough memory to allocate %d bytes of memory\n", size);
	void* frames[64];
	int nframes = backtrace(frames, sizeof(frames) / sizeof(frames[0]));
	backtrace_symbols_fd(frames, nframes, STDERR_FILENO);
	exit (-1);
	return 0;
}

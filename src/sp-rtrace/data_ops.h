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

#ifndef DATA_OPS_H
#define DATA_OPS_H

/**
 * @file data_ops.h
 *
 * Contains templates of the data record operation
 * functions used by data containers (dlist, htable, sarray).
 */

/*
 * Binary operation accepting 2 data records and returning long
 * value. For example record comparison functions use binary
 * operation template.
 */
typedef long (*op_binary_t)(void*, void*);

/*
 * Unary operation accepting 1 data record and returning long
 * value. For example hash calculation and record freeing
 * functions use unary operation template.
 */
typedef long (*op_unary_t)(void*);

#endif

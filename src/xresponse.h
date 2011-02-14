/*
 * xresponse - Interaction latency tester,
 *
 * Written by Ross Burton & Matthew Allum
 *              <info@openedhand.com>
 *
 * Copyright (C) 2005,2011 Nokia
 *
 * Licensed under the GPL v2 or greater.
 *
 * Window detection is based on code that is Copyright (C) 2007 Kim Woelders.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifndef _XRESPONSE_H_
#define _XRESPONSE_H_

#include <stdbool.h>

/* resource alias for the root window */
#define ROOT_WINDOW_RESOURCE	"*SCREEN*"
#define RESPONSE_APPLICATION_NAME "*APPLICATION*"

#define CMD_STRING_MAXLEN 256

typedef struct Rectangle {
	int x, y, width, height;
} Rectangle;


/* options data structure */
typedef struct {
	int damage_wait_secs; /* Max time to collect damamge */
	Rectangle interested_damage_rect; /* Damage rect to monitor */
	int break_on_damage; /* break on the specified damage event */
	bool abort_wait; /* forces to abort damage wait loop if set to true */

	Rectangle exclude_rect;  /* Damage rectangle for filtering rules */
	unsigned int exclude_size; /* Damage rectangle size for filtering rules */
	unsigned int exclude_rules; /* damage filtering rules */

	unsigned int break_timeout;
} options_t;


extern options_t options;

/**
 * Checks if the difference between two timestamps is greater or equal
 * than the specified timeout in milliseconds.
 *
 * @param tv1[in]   the first timestamp (start value).
 * @param tv2[in]   the second timestamp (end value).
 * @param timeout   the timeout to check (in milliseconds).
 * @return          true if the difference between tiemestamps are greater or equal than specified.
 */
bool check_timeval_timeout(struct timeval* tv1, struct timeval* tv2, int timeout);


#endif

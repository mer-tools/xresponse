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

/**
 * @file xhandler.h
 * X event handler.
 *
 * xhandler.c|h provides X display connection initialization, setup, error
 * handling and utility functionality.
 */

#ifndef _XHANDLER_H_
#define _XHANDLER_H_

#include <stdbool.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/Xutil.h>
#include <X11/Xproto.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/record.h>

typedef struct {
	int damage_event_num; /* Damage Ext Event ID */

	Atom timestamp_atom; /* Atom for getting server time */

	Display* display;  /* the connected display */

} xhandler_t;

extern xhandler_t xhandler;

/**
 *
 */
typedef union {
	XEvent ev;
	XDamageNotifyEvent dev;
	XCreateWindowEvent cev;
	XUnmapEvent uev;
	XMapEvent mev;
	XDestroyWindowEvent dstev;
} xevent_t;

/**
 * Set up Display connection, required extensions and req other X bits
 */
bool xhandler_init(const char *dpy_name);

/**
 * Releases resources allocated by X event handler.
 */
void xhandler_fini();


/**
 * Eat all Damage events in the X event queue.
 */
void xhandler_eat_damage();


/**
 * Get an X event with a timeout ( in secs ). The timeout is
 * updated for the number of secs left.
 */
bool xhandler_get_xevent_timed(XEvent *event_return, struct timeval *tv);

/**
 * Get the current timestamp from the X server.
 */
Time xhandler_get_server_time();



#endif

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

#ifndef _XEMU_H_
#define _XEMU_H_

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/Xutil.h>
#include <X11/Xproto.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/record.h>

enum { /* for 'dragging' */
	XR_BUTTON_STATE_NONE, XR_BUTTON_STATE_PRESS, XR_BUTTON_STATE_RELEASE
};


typedef struct {
	XDevice* keyboard;
	XDevice* pointer;
	Display* display;

} xemu_t;

extern xemu_t xemu;

#define MAX_KEYSYM 65536

/**
 * Initializes mouse/keyboard emulation subsystem.
 * @param dpy
 */
void xemu_init(Display* dpy);

/**
 * Releases resources allocated by mouse/keyboard emulation subsystem.
 */
void xemu_fini();


/* Simulate pressed key(s) to generate thing character
 * Only characters where the KeySym corresponds to the Unicode
 * character code and KeySym < MAX_KEYSYM are supported,
 * except the special character 'Tab'. */
Time xemu_send_string(char *thing_in);

/* Load keycodes and modifiers of current keyboard mapping into arrays,
 * this is needed by the send_string function */
void xemu_load_keycodes();

Time xemu_send_key(char *thing, unsigned long delay);

/**
 * 'Fakes' a mouse click, returning time sent.
 */
Time xemu_button_event(int x, int y, int delay);

Time xemu_drag_event(int x, int y, int button_state, int delay);


#endif

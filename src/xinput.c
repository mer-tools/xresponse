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

#define _GNU_SOURCE

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "xinput.h"
#include "application.h"
#include "window.h"
#include "report.h"


/* the xrecord data */
xrecord_t xrecord = {
		.display = NULL,
		.context = 0,
		.enabled = false,
		.motion = false,
};

/* Needed for client window checking */
static Atom wmstate_atom = None;

/* the default display */
static Display* display = NULL;

/**
 */
static bool window_has_property(Display * dpy, Window win, Atom atom)
{
	Atom type_ret;
	int format_ret;
	unsigned char *prop_ret;
	unsigned long bytes_after, num_ret;

	type_ret = None;
	prop_ret = NULL;
	XGetWindowProperty(dpy, win, atom, 0, 0, False, AnyPropertyType, &type_ret, &format_ret, &num_ret, &bytes_after,
			&prop_ret);
	if (prop_ret)
		XFree(prop_ret);

	return (type_ret != None) ? true : false;
}

/*
 * Check if window is viewable
 */
static bool window_is_viewable(Display * dpy, Window win)
{
	bool rc;
	XWindowAttributes xwa;
	XGetWindowAttributes(dpy, win, &xwa);
	rc = (xwa.class == InputOutput) && (xwa.map_state == IsViewable);

	return rc;
}

/**
 * Finds client window for window reported at the cursor location.
 */
static Window window_find_client(Display* dpy, Window window)
{
	Window root, parent, win = None;
	Window *children;
	unsigned int n_children;
	int i;

	if (!XQueryTree(dpy, window, &root, &parent, &children, &n_children))
		return None;
	if (!children)
		return None;

	display = dpy;

	/* Check each child for WM_STATE and other validity */
	for (i = (int) n_children - 1; i >= 0; i--) {
		if (!window_is_viewable(dpy, children[i])) {
			children[i] = None; /* Don't bother descending into this one */
			continue;
		}
		if (!window_has_property(dpy, children[i], wmstate_atom)) {
			continue;
		}

		/* Got one */
		win = children[i];
		break;
	}

	/* No children matched, now descend into each child */
	if (win == None) {
		for (i = (int) n_children - 1; i >= 0; i--) {
			if (children[i] == None)
				continue;
			win = window_find_client(dpy, children[i]);
			if (win != None)
				break;
		}
	}
	XFree(children);

	return win;
}

/**
 * Finds client window at the cursor location.
 *
 */
static Window get_window_at_cursor(Display* dpy)
{
	Window root, child, client;
	int root_x, root_y, win_x, win_y;
	unsigned mask;

	XQueryPointer(dpy, DefaultRootWindow(dpy), &root, &child, &root_x, &root_y, &win_x, &win_y, &mask);

	if (!wmstate_atom)
		return child;

	client = window_find_client(dpy, child);
	return client == None ? child : client;
}


static void xrecord_callback(XPointer closure, XRecordInterceptData* data)
{
	if (XRecordFromServer == data->category) {
		Display* dpy = (Display*) closure;
		xEvent* xev = (xEvent*) data->data;
		int type = xev->u.u.type;
		static int x, y;
		window_t* win;
		application_t* app = NULL;
		char extInfo[256] = "";

		switch (type) {
		case ButtonPress:
			win = window_find(get_window_at_cursor(dpy));

			if (win) {
				app = win->application;
				sprintf(extInfo, "(%s)", app->name);
			}
			report_add_message(xev->u.keyButtonPointer.time, "Button %x pressed at %dx%d %s\n", xev->u.u.detail, x, y,
					extInfo);
			if (response.timeout) {
				application_set_user_action("press (%dx%d) %s", x, y, extInfo);
				application_response_reset(xev->u.keyButtonPointer.time);
				application_response_start(app);
			}
			break;

		case ButtonRelease:
			/* report any button press related response times */
			application_response_report();

			/**/
			win = window_find(get_window_at_cursor(dpy));
			if (win) {
				sprintf(extInfo, "(%s)", win->application->name);
			}
			report_add_message(xev->u.keyButtonPointer.time, "Button %x released at %dx%d %s\n", xev->u.u.detail, x, y,
					extInfo);
			if (response.timeout) {
				application_set_user_action("release (%dx%d) %s", x, y, extInfo);
				application_response_reset(xev->u.keyButtonPointer.time);
			}
			break;

		case KeyPress:
			report_add_message(xev->u.keyButtonPointer.time, "Key %s pressed\n", XKeysymToString(XKeycodeToKeysym(dpy,
					xev->u.u.detail, 0)));

			if (response.timeout) {
				application_set_user_action("key press (%s)",  XKeysymToString(XKeycodeToKeysym(dpy, xev->u.u.detail, 0)));
				application_response_reset(xev->u.keyButtonPointer.time);
				application_response_start(app);
			}
			break;

		case KeyRelease:
			/* report any button press related response times */
			application_response_report();

			report_add_message(xev->u.keyButtonPointer.time, "Key %s released\n", XKeysymToString(XKeycodeToKeysym(dpy,
					xev->u.u.detail, 0)));
			if (response.timeout) {
				application_set_user_action("key release (%s)",  XKeysymToString(XKeycodeToKeysym(dpy, xev->u.u.detail, 0)));
				application_response_reset(xev->u.keyButtonPointer.time);
			}
			break;

		case MotionNotify:
			if (xrecord.motion) {
				report_add_message(xev->u.keyButtonPointer.time, "Pointer moved to %dx%d\n",
					xev->u.keyButtonPointer.rootX, xev->u.keyButtonPointer.rootY);
			}
			x = xev->u.keyButtonPointer.rootX;
			y = xev->u.keyButtonPointer.rootY;
			break;

		default:
			fprintf(stderr, "Unknown device event type %d\n", type);
			break;
		}

	}

	XRecordFreeData(data);
}

/*
 * Public API implementation.
 */


void xinput_init(Display* dpy)
{
	if (xrecord.enabled) return;

	int major = 0, minor = 0;
	if (!XRecordQueryVersion(dpy, &major, &minor)) {
		fprintf(stderr, "Can't monitor user input without xrecord extension\n");
		exit(-1);
	}

	XRecordClientSpec clients = XRecordAllClients;
	int num_ranges = 3;
	int iRange = 0;
	XRecordRange** rec_range = 0;

	xrecord.display = XOpenDisplay(getenv("DISPLAY"));
	if (!xrecord.display) {
		fprintf(stderr, "Failed to open event recording display connection\n");
		exit(-1);
	}
	/* prepare event range data */
	rec_range = (XRecordRange**) g_malloc(sizeof(XRecordRange*) * num_ranges);
	XRecordRange* range;
	XRecordRange** ptrRange = rec_range;

	range = XRecordAllocRange();
	range->device_events.first = KeyPress;
	range->device_events.last = KeyRelease;
	*ptrRange++ = range;

	range = XRecordAllocRange();
	range->device_events.first = ButtonPress;
	range->device_events.last = ButtonRelease;
	*ptrRange++ = range;

	range = XRecordAllocRange();
	range->device_events.first = MotionNotify;
	range->device_events.last = MotionNotify;
	*ptrRange++ = range;

	/* */
	xrecord.context = XRecordCreateContext(dpy, XRecordFromServerTime, &clients, 1, rec_range, num_ranges);
	XFlush(dpy);
	XFlush(xrecord.display);
	XRecordEnableContextAsync(xrecord.display, xrecord.context, xrecord_callback, (XPointer) dpy);

	/* release range data */
	for (iRange = 0; iRange < num_ranges; iRange++) {
		free(rec_range[iRange]);
	}
	free(rec_range);

	xrecord.enabled = true;

	wmstate_atom = XInternAtom(dpy, "WM_STATE", False);
	display = dpy;
}


void xinput_fini()
{
	if (xrecord.enabled) {
		XRecordDisableContext(display, xrecord.context);
		XRecordFreeContext(display, xrecord.context);
		XFlush(display);
		XCloseDisplay(xrecord.display);
	}
}

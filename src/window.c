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

#include "window.h"

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xresponse.h"
#include "application.h"

/**
 * Windows monitor data structure.
 */
typedef struct {
	/* window list */
	dlist_t windows;
	/* the connected display */
	Display* display;

	/* damage reporting level */
	int damage_level;
} monitor_t;

static monitor_t monitor = {
		.display = NULL,
		.damage_level = XDamageReportBoundingBox,
};


/**
 * Releases resources allocated by the window.
 * @param[in] win  the window to free.
 */
static void window_free(window_t* win)
{
	if (win->damage) XDamageDestroy(monitor.display, win->damage);
	if (win->application) application_release(win->application);
	free(win);
}


/**
 * Compares window id with the specified id.
 * @param[in] win     the window to check.
 * @param[in] window  the id to compare.
 * @return            0 if id's match.
 */
static long compare_window(window_t* win, Window window)
{
	return win->window == window ? 0 : 1;
}

/**
 * Start monitoring the specified window.
 *
 * @param[in] win  the window to monitor.
 * @return         true if the window monitoring started successfully.
 */
static bool window_start_monitor(window_t* win)
{
	win->damage = XDamageCreate(monitor.display, win->window, monitor.damage_level);
	if (!win->damage) {
		fprintf(stderr, "XDamageCreate failed for window %lx, application %s\n", win->window,
				win->application ? win->application->name : "(null)");
		return false;
	}
	if (win->window != DefaultRootWindow(monitor.display)) {
		XSelectInput(monitor.display, win->window, StructureNotifyMask);
	}
	return true;
}


/**
 * Attempts to monitor the specified window and removes it from
 * the list if failed.
 * @param[in] win   the window to monitor.
 */
static void window_monitor_or_remove(window_t* win)
{
	if (!window_start_monitor(win)) {
		window_remove(win);
	}
}


/*
 * Public API implementation.
 */

void window_init(Display* display)
{
	dlist_init(&monitor.windows);
	monitor.display = display;
}


void window_fini()
{
	dlist_free(&monitor.windows, (op_unary_t)window_free);
}


void window_remove(window_t* win)
{
	dlist_remove(&monitor.windows, win);
	window_free(win);
}

void window_set_damage_level(int level)
{
	monitor.damage_level = level;
}


window_t* window_find(Window window)
{
	return dlist_find(&monitor.windows, (void*)window, (op_binary_t)compare_window);
}


const char* window_get_resource_name(Window window)
{
	static char resource_name[PATH_MAX];
	XClassHint classhint;

	/* alias the root window to '*' resource */
	if (window == DefaultRootWindow(monitor.display)) {
		strcpy(resource_name, ROOT_WINDOW_RESOURCE);
		return resource_name;
	}
	if (XGetClassHint(monitor.display, window, &classhint)) {
		strncpy(resource_name, classhint.res_name, PATH_MAX);
		resource_name[PATH_MAX - 1] = '\0';
		XFree(classhint.res_name);
		XFree(classhint.res_class);
		return resource_name;
	}
	return NULL;
}


window_t* window_try_monitor(Window window)
{
	/* check if the windows is already monitored */
	if (window_find(window)) return NULL;

	const char* resource = window_get_resource_name(window);
	if (resource) {
		application_t* app = application_try_monitor(resource);
		if (app) {
			window_t* win = window_add(window, app);
			if (win && window_start_monitor(win)) {
				return win;
			}
			window_remove(win);
		}
	}
	return NULL;
}


void window_monitor_all()
{
	dlist_foreach(&monitor.windows, (op_unary_t)window_monitor_or_remove);
}


void window_try_monitor_children(Window window)
{
	Window root_win, parent_win;
	Window *child_list;
	unsigned int num_children;
	int iWin;

	if (!XQueryTree(monitor.display, window, &root_win, &parent_win, &child_list, &num_children)) {
		fprintf(stderr, "Cannot query window tree (%lx)\n", window);
		return;
	}
	for (iWin = 0; iWin < num_children; iWin++) {
		if (!window_try_monitor(child_list[iWin])) {
			window_try_monitor_children(child_list[iWin]);
		}
	}
	if (child_list) XFree(child_list);
}


bool window_empty()
{
	return dlist_first(&monitor.windows) == NULL;
}


window_t* window_add(Window window, application_t* application)
{
	window_t* win = dlist_create_node(sizeof(window_t));
	win->window = window;
	win->damage = 0;
	win->application = application;
	dlist_add(&monitor.windows, win);
	return win;
}


/**
 * Dump the contents of the monitored windows list.
 *
 * Used for debug purposes.
 * @return
 */
/*
 static void
 window_dump() {
 window_t* win;
 for (win = Windows; win - Windows < WindowIndex; win++) {
 printf("[window] 0x%lx (%s)\n", win->window, win->application->name);
 }
 }
 */




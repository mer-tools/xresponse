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

#ifndef _WINDOW_H_
#define _WINDOW_H_

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/Xutil.h>
#include <X11/Xproto.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/record.h>

#include "application.h"
#include "sp-rtrace/dlist.h"

/**
 * Window data structure
 */
typedef struct _window_t {
	/* double linked list support */
	dlist_node_t _node;
	/* window data */
	Window window; /* the window id */
	Damage damage; /* the damage context */
	application_t* application; /* the owner application */
} window_t;


/**
 * Initializes window monitor.
 *
 * @param[in] display   the connected display.
 */
void window_init(Display* display);


/**
 * Releases resources allocated by window monitor.
 */
void window_fini();


/**
 * Sets the damage reporting level.
 *
 * The damage level is defined in /usr/include/X11/extensions/Xdamage.h
 * and can be one of the following: XDamageReportRawRectangles,
 * XDamageReportDeltaRectangles, XDamageReportNonEmpty.
 * @param[in] level   the damage reporting level.
 */
void window_set_damage_level(int level);


/**
 * Searches monitored window list for the specified window.
 *
 * @param window[in]  the window to search.
 * @return            the located window or NULL otherwise.
 */
window_t* window_find(Window window);


/**
 * Retrieves resource (application) name associated with the window.
 *
 * The root windows is aliased as '*'.
 * @param[in] window  the window which resource name should be retrieved.
 * @return            a reference to the resource name or NULL. This name will be changed
 *                    by another window_get_resource_name call, so don't store the reference
 *                    but copy contents if needed.
 */
const char* window_get_resource_name(Window window);


/**
 * Try to monitor the specified window.
 *
 * This method checks if the specified window must/can be monitored and
 * will start monitoring it.
 * @param[in] window   the window to monitor.
 * @return             a reference to the window data structure if monitoring
 *                     started successfully. NULL otherwise.
 */
window_t* window_try_monitor(Window window);



/**
 * Start to monitor all windows in list.
 *
 * This function is called in the beginning, to start monitoring all windows
 * which was added manually with --id option.
 */
void window_monitor_all();



/**
 * Tries to monitor children of the specified window.
 *
 * This function will try to monitor every child of the specified window.
 * If monitoring a child fails, it will try to monitor its children.
 * @param[in] window  the window which children to monitor.
 */
void window_try_monitor_children(Window window);


/**
 * Checks if the monitored windows list is empty.
 *
 * @return   true if the monitored windows list is empty.
 */
bool window_empty();


/**
 * Removes window from monitored list and frees it.
 * @param[in] win   the window to remove.
 */
void window_remove(window_t* win);

/**
 * Adds window to monitored window list.
 *
 * @param[in] window   the window being monitored.
 * @return             the added window.
 */
window_t* window_add(Window window, application_t* application);


#endif


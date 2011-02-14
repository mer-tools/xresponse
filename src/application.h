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
 * @file application.h
 * Application damage monitoring and user action response tracking.
 *
 * application.c|h files provides 'to-be-monitored' application list and response
 * measurement management.
 */

#ifndef _APPLICATION_H_
#define _APPLICATION_H_

#include "sp-rtrace/dlist.h"

#include <stdbool.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/Xutil.h>
#include <X11/Xproto.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/record.h>

/**
 * Application data structure.
 */
typedef struct application_t {
	/* double linked list support */
	dlist_node_t _node;

	/* the application resource name (binary file) */
	char* name;

	/* the first damage event after user action */
	XDamageNotifyEvent first_damage_event;

	/* the last damage event after user action */
	XDamageNotifyEvent last_damage_event;

	/* reference counter */
	int ref;
} application_t;


/**
 * Response data structure.
 */
typedef struct {
	/* the last user action in user friendly format */
	char last_action_name[256];

	/* application response checking timeout */
	unsigned int timeout;

	/* last user action time */
	Time last_action_time;
	/* local timestamp of the last user action */
	struct timeval last_action_timestamp;

	/* the application for response data monitoring */
	application_t* application;
} response_t;

/* the response data */
extern response_t response;


/**
 * Adds an application to monitored applications list if necessary and increments its
 * reference counter.
 *
 * Monitoring an application causes xresponse to watch damage for all application's
 * windows.
 * @param name[in]    the application name.
 * @return            a reference to the added application.
 */
application_t* application_monitor(const char* name);

/**
 * Initializes the application monitoring.
 */
void application_init();


/**
 * Releases resources allocated by application monitoring.
 */
void application_fini();


/**
 * Starts to monitor all windows associated to the monitored applications.
 */
void application_start_monitor();


/**
 * Forces all applications to be monitored.
 *
 * By default only applications that are explicitly added to to-be-monitored
 * list are monitored. Enabling monitor_all flag causes all currently running
 * and future applications to be monitored.
 */
void application_set_monitor_all(bool value);


/**
 * Reports application response times collected from the last user input.
 */
void application_response_report();


/**
 * Stores last user action description.
 *
 * The last user action description is used for reporting application response times
 * @param format[in]   the format string (see printf);
 * @param ...
 */
void application_set_user_action(const char* format, ...);


/**
 * Initializes application response check.
 *
 * This function is called in response monitoring mode after every button press.
 * @param[in] app   the application owning the clicked window (can be NULL).
 */
void application_response_start(application_t* app);


/**
 * Registers application damage event in response monitoring mode.
 *
 * @param[in] app  the application the damage occured in.
 * @param[in] dev  the damage event.
 */
void application_register_damage(application_t* app, XDamageNotifyEvent* dev);

/**
 * Monitor the screen (all opened application).
 */
void application_monitor_screen();


/**
 * Checks if the monitored application list is empty.
 *
 * @return   true if the monitored application list is empty.
 */
bool application_empty();

/**
 * Attempts to monitor the specified application.
 *
 * The application is monitored either when it's on to-be-monitored list or
 * when monitor_all flag is enabled. In the second case the application is
 * placed on the to-be-monitored list during its lifetime.
 * The found/added application is returned. Otherwise (if the application
 * should not be monitored) a NULL value is returned.
 * @param[in] resource   the application name.
 * @return               the application or NULL, if the resource is not monitored.
 */
application_t* application_try_monitor(const char* resource);

/**
 * Increases application reference counter.
 *
 * @param app[in]   the application.
 * @return
 */
void application_addref(application_t* app);


/**
 * Decrements application reference counter and removes the application from monitored
 * applications list if necessary.
 *
 * @param app[in]   the application to release.
 * @return
 */
void application_release(application_t* app);


/**
 * Resets application response monitoring.
 *
 * @param[in] timestamp  the current timestamp.
 */
void application_response_reset(Time timestamp);

#endif

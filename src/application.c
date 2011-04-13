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

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/time.h>
#include <string.h>
#include <execinfo.h>
#include <unistd.h>

#include <glib.h>

#include "application.h"
#include "xresponse.h"
#include "report.h"
#include "window.h"
#include "xhandler.h"

/**
 * The application management data.
 */
typedef struct {
	/* to-be-monitored application list */
	GList* applications;

	/* the screen application */
	application_t* screen;

	/* monitor-all flag, specifying if all applications must be monitored */
	bool all;
} monitor_t;


static monitor_t monitor = {
		.screen = NULL,
		.all = false,
};



/* application response data */
response_t response = {
		.last_action_name = "",
		.timeout = 0,
		.last_action_timestamp = {
				.tv_sec = 0,
				.tv_usec = 0,
		},
		.last_action_time = 0,
		.application = NULL,
};


/**
 * Adds a new application to the monitored application list.
 *
 * When a new window is created xresponse checks the monitored application list
 * if the window belongs to a monitored application and its damage events must
 * be monitored.
 * @param name[in]          the application name.
 * @return                  the created application object.
 */
static application_t* application_add(const char* name)
{
	application_t* app = g_slice_new(application_t);
	app->name = g_strdup(name);
	app->ref = 1;
	memset(&app->first_damage_event, 0, sizeof(XDamageNotifyEvent));
	memset(&app->last_damage_event, 0, sizeof(XDamageNotifyEvent));
	monitor.applications = g_list_prepend(monitor.applications, app);
	return app;
}


/**
 * Releases resources allocated by the application.
 *
 * @param[in] app  the application to free.
 */
static void application_free(application_t* app, void* __attribute__((unused)) data)
{
	if (app->name) free(app->name);
	g_slice_free(application_t, app);
}


/**
 * Decrements application reference counter and removes the application from monitored
 * applications list if necessary.
 *
 * @param[in] app   the application to release.
 * @param[in] data  unused
 * @return
 */
static void application_release_data(application_t* app, void* __attribute__((unused)) data)
{
	if (app  && !(--app->ref) ) {
		monitor.applications = g_list_remove(monitor.applications, app);
		application_free(app, NULL);
	}
}


/**
 * Compares application name with the specified string.
 *
 * This function is used to implement application_find() functionality.
 * @param[in] app    The application to compare.
 * @param[in] name   the string to match.
 * @return           0 if the name matches the specified string.
 */
static gint compare_application_name(application_t* app, const char* name, void* __attribute__((unused)) data)
{
	return strcmp(app->name, name);
}


/**
 * Searches application list for the specified application.
 *
 * @param name[in]  the application name.
 * @return 			reference to the located application or NULL otherwise.
 */
static application_t* application_find(const char* name)
{
	GList* node = g_list_find_custom(monitor.applications, name, (GCompareFunc)compare_application_name);
	return node ? node->data : NULL;
}


/**
 * Resets application damage events.
 * 
 * @param app[in]		the application.
 */
static void application_reset_events(application_t* app) 
{
	if (app->first_damage_event.timestamp) {
		app->first_damage_event.timestamp = 0;
		app->last_damage_event.timestamp = 0;
		application_release_data(app, NULL);
	}
}

/**
 * Reports damage events for the specified application at the given timestamp.
 *
 * @param[in] app          the specified application.
 */
static void report_app_damage_event(application_t* app, void* __attribute__((unused)) data)
{
	if (app->first_damage_event.timestamp) {
		report_add_message_forced("\t%32s updates: first %5ims, last %5ims\n",
				app->name ? app->name : "(unknown)",
				app->first_damage_event.timestamp - response.last_action_time, app->last_damage_event.timestamp - response.last_action_time);
		app->first_damage_event.timestamp = 0;
		application_release_data(app, NULL);
	}
}


/**
 * Reports the response data of applications.
 *
 * @param[in] timestamp  the report event timestamp.
 * @return
 */
static void application_report_response_data()
{
	if (response.application && !response.application->first_damage_event.timestamp) {
		fprintf(stderr,
				"Warning, during the response timeout the monitored application %s did not receive any damage events."
					"Using screen damage events instead.\n", response.application->name);
		if (monitor.screen && monitor.screen->first_damage_event.timestamp) {
			response.application->first_damage_event.timestamp = monitor.screen->first_damage_event.timestamp;
			response.application->last_damage_event.timestamp = monitor.screen->last_damage_event.timestamp;
			application_addref(response.application);
		}
	}

	g_list_foreach(monitor.applications, (GFunc)report_app_damage_event, NULL);
	report_add_message_forced("\n");
	application_release_data(response.application, NULL);
}



/*
 * Public API
 */


application_t* application_monitor(const char* name)
{
	application_t* app = application_find(name);
	if (!app) {
		app = application_add(name);
	}
	else {
		application_addref(app);
	}
	return app;
}


void application_init()
{
	monitor.applications = NULL;
	monitor.screen = NULL;
	response.application = NULL;
}


void application_fini()
{
	g_list_foreach(monitor.applications, (GFunc)application_free, NULL);
	monitor.screen = NULL;
	response.application = NULL;
}




void application_start_monitor()
{
	Window win = DefaultRootWindow(xhandler.display);
	window_try_monitor(win);
	window_try_monitor_children(win);
}


void application_set_monitor_all(bool value)
{
	monitor.all = value;
}

application_t* application_try_monitor(const char* resource)
{
	application_t* app = application_find(resource);
	if (app) {
		application_addref(app);
		return app;
	}
	if (monitor.all) {
		return application_monitor(resource);
	}
	return NULL;
}


void application_response_report()
{
	if (response.last_action_time) {
		/* In raw mode only data with 0 timestamps are printed. That was done to suppress
		 * the standard damage reporting output.
		 * TODO: much better would be not generating damage reports in raw mode instead of
		 * just suppressing them at reporter level.
		 */
		report_add_message_forced("Device response time to %s:\n", response.last_action_name);
		application_report_response_data();
		response.last_action_time = 0;
		response.last_action_timestamp.tv_sec = 0;
		response.last_action_timestamp.tv_usec = 0;
	}
}

void application_response_reset(Time timestamp)
{
	if (response.timeout) {
		if (response.last_action_time) {
			report_add_message_forced("Warning, new user event received in the middle of update. "
				"It is possible that update time is not correct.\n");

			g_list_foreach(monitor.applications, (GFunc)application_reset_events, NULL);
			application_release_data(response.application, NULL);
		}
		response.last_action_time = timestamp;
		gettimeofday(&response.last_action_timestamp, NULL);
	}
}


void application_set_user_action(const char* format, ...)
{
	va_list ap;
	va_start(ap, format);
	vsnprintf(response.last_action_name, sizeof(response.last_action_name), format, ap);
	va_end(ap);
}

void application_response_start(application_t* app)
{
	if (monitor.all) {
		response.application = app;
	}

	/* Increasing response application reference counter for both - press and release events */
	application_addref(response.application);
	application_addref(response.application);
}

void application_register_damage(application_t* app, XDamageNotifyEvent* dev)
{
	if (app->first_damage_event.timestamp < response.last_action_time) {
		app->first_damage_event = *dev;
		application_addref(app);
	}
	app->last_damage_event = *dev;
}

void application_monitor_screen()
{
	monitor.screen = application_monitor(ROOT_WINDOW_RESOURCE);
}

bool application_empty()
{
	return monitor.applications == NULL;
}

void application_addref(application_t* app)
{
	if (app) {
		app->ref++;
	}
}

void application_release(application_t* app)
{
	application_release_data(app, NULL);
}

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
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <sys/time.h>

#include <glib.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/Xutil.h>
#include <X11/Xproto.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/record.h>

#include "scheduler.h"
#include "xresponse.h"
#include "xemu.h"

typedef struct {
	/* scheduled event list */
	GQueue events;

	/* the connected display */
	Display* display;

	/* timestamp of the last processed event */
	struct timeval last_timestamp;
} scheduler_t;


static scheduler_t scheduler = {
		.display = NULL,
		.last_timestamp = {
				.tv_sec = 0,
				.tv_usec = 0,
		},
};


static void event_free(event_t* event, void* __attribute__((unused)) data)
{
	g_slice_free(event_t, event);
}


/**
 * Emulates user input event.
 *
 * @param[in] event   the event to emulate.
 * @return
 */
static void fake_event(event_t* event)
{
	static int xPos = 0;
	static int yPos = 0;

	switch (event->type) {
		case SCHEDULER_EVENT_BUTTON: {
			int axis[XEMU_POINTER_MAX_AXES] = {xPos, yPos};
			XTestFakeDeviceButtonEvent(scheduler.display, event->device, event->param1,
					event->param2, axis, event->naxes, CurrentTime);
			break;
		}

		case SCHEDULER_EVENT_KEY: {
				int axis[XEMU_POINTER_MAX_AXES] = {xPos, yPos};
				XTestFakeDeviceKeyEvent(scheduler.display, event->device, event->param1,
						event->param2, axis, event->naxes, CurrentTime);
				break;
		}

		case SCHEDULER_EVENT_MOTION: {
			xPos = event->param1;
			yPos = event->param2;
			int axis[XEMU_POINTER_MAX_AXES] = {xPos, yPos};
			XTestFakeDeviceMotionEvent(scheduler.display, event->device, False, 0,
					axis, event->naxes, CurrentTime);
			break;
		}
	}
}



/*
 * Public API implementation.
 */


void scheduler_init(Display* display)
{
	scheduler.display = display;
	g_queue_init(&scheduler.events);
}


void scheduler_fini()
{
	g_queue_foreach(&scheduler.events, (GFunc)event_free, NULL);
}


event_t* scheduler_add_event(int type, XDevice* device, int param1, int param2, int delay, int naxes)
{
	event_t* event = g_slice_new(event_t);
	event->type = type;
	event->device = device;
	event->param1 = param1;
	event->param2 = param2;
	event->delay = delay;
	event->naxes = naxes;
	g_queue_push_tail(&scheduler.events, event);
	return event;
}


int scheduler_process(struct timeval* timestamp)
{
	if (!scheduler.last_timestamp.tv_sec) {
		scheduler.last_timestamp = *timestamp;
	}

	event_t* event;
	while ( (event = g_queue_peek_head(&scheduler.events)) &&
			check_timeval_timeout(&scheduler.last_timestamp, timestamp, event->delay) ) {
		fake_event(event);
		g_queue_pop_head(&scheduler.events);

		struct timeval delay = {
		    .tv_usec = event->delay * 1000,
		};
		timeradd(&scheduler.last_timestamp, &delay, &scheduler.last_timestamp);

		event_free(event, NULL);
	}
	if (!event) return 0;
	struct timeval diff;
	timersub(timestamp, &scheduler.last_timestamp, &diff);
	int diff_val = diff.tv_sec * 1000 + diff.tv_usec / 1000;
	return event->delay - diff_val;
}



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

typedef struct {
	/* scheduled event list */
	dlist_t events;

	/* the connected display */
	Display* display;

	/* the current timestamp */
	struct timeval* ptimestamp;

	/* timestamp of the last processed event */
	struct timeval last_timestamp;
} scheduler_t;


static scheduler_t scheduler = {
		.display = NULL,
		.last_timestamp = {
				.tv_sec = 0,
				.tv_usec = 0,
		},
		.ptimestamp = NULL,
};



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
		int axis[2] = { xPos, yPos };
		XTestFakeDeviceButtonEvent(scheduler.display, event->device, event->param1, event->param2, axis, 2, CurrentTime);
		break;
	}

	case SCHEDULER_EVENT_KEY:
		XTestFakeDeviceKeyEvent(scheduler.display, event->device, event->param1, event->param2, NULL, 0, CurrentTime);
		break;

	case SCHEDULER_EVENT_MOTION:
		xPos = event->param1;
		yPos = event->param2;
		{
			int axis[2] = { xPos, yPos };
			XTestFakeDeviceMotionEvent(scheduler.display, event->device, False, 0, axis, 2, CurrentTime);
			break;
		}
	}
}


/**
 * Emulates event and removes it from the scheduler.
 *
 * @param[in] event   the event to emulate.
 */
static void process_event(event_t* event)
{
	fake_event(event);
	dlist_remove(&scheduler.events, event);
	free(event);
	scheduler.last_timestamp = *scheduler.ptimestamp;
}

/**
 * Checks if the event must be emulated.
 *
 * This function checks if enough time has passed since the last event emulation
 * so the current event can be emulated.
 * @param[in] event   the event to check.
 * @return            0 - event must be emulated.
 */
static long check_delay(event_t* event)
{
	return check_timeval_timeout(&scheduler.last_timestamp, scheduler.ptimestamp, event->delay);
}

/*
 * Public API implementation.
 */


void scheduler_init(Display* display)
{
	scheduler.display = display;
	dlist_init(&scheduler.events);
}


void scheduler_fini()
{
	dlist_free(&scheduler.events, (op_unary_t)free);
}


event_t* scheduler_add_event(int type, XDevice* device, int param1, int param2, int delay)
{
	event_t* event = dlist_create_node(sizeof(event_t));
	event->type = type;
	event->device = device;
	event->param1 = param1;
	event->param2 = param2;
	event->delay = delay;
	dlist_add(&scheduler.events, event);
	return event;
}


void scheduler_process(struct timeval* timestamp)
{
	if (!scheduler.last_timestamp.tv_sec) {
		scheduler.last_timestamp = *timestamp;
	}
	scheduler.ptimestamp = timestamp;
	dlist_foreach_in(&scheduler.events, dlist_first(&scheduler.events), (op_unary_t)check_delay, (op_unary_t)process_event);
}



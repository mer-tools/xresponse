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
 * @file scheduler.h
 * Event scheduler for user input event emulation.
 *
 * The 'faked' user input events are stored in queue and are fired
 * when the current timestamp is greater than the last event timestamp plus
 * the next event delay. This allows to emulate any user action sequence with
 * custom delays between input events.
 */

#ifndef _SCHEDULER_H_
#define _SCHEDULER_H_

/**
 * The input event types.
 */
enum scheduler_event_type_t {
	SCHEDULER_EVENT_NONE = 0,
	/* button press/release */
	SCHEDULER_EVENT_BUTTON,
	/* key press/release */
	SCHEDULER_EVENT_KEY,
	/* cursor movement */
	SCHEDULER_EVENT_MOTION
};

/**
 * Scheduler event structure
 */
typedef struct event_t {
	/* the event type, see scheduler_event_type_t enum */
	int type;

	/* the target device (keyboard/mouse) */
	XDevice* device;

	/* first parameter. button/key or x coordinate */
	int param1;

	/* second parameter. True/False (down/up) or y coordinate */
	int param2;

	/* the event delay from the last event (in milliseconds) */
	int delay;

	/* number of axes supported by device/event */
	int naxes;
} event_t;


/**
 * Initializes scheduler.
 *
 * @param[in] display   the connected display.
 */
void scheduler_init(Display* display);


/**
 * Releases resources allocated by the scheduler.
 */
void scheduler_fini();


/**
 * Adds new event to the scheduler.
 *
 * @param[in] type    the event type (see scheduler_event_type_t enum)
 * @param[in] device  the input device.
 * @param[in] param1  the first parameter. For button and key events it's the button/key code.
 *                    For motion events it's the x coordinate of the new cursor location.
 * @param[in] param2  the second parameter. For button and key events its True/False indicating
 *                    if the button/key was pressed or released. For motion events it's the y
 *                    coordinate of the new cursor location.
 * @param[in] delay   the delay time since the last event (in milliseconds)
 * @param[in] naxes   the number of axes supported by device/event
 * @return            the added event or NULL in the case of an error.
 */
event_t* scheduler_add_event(int type, XDevice* device, int param1, int param2, int delay, int naxes);


/**
 * Processes events until the specified timestamp.
 *
 * This function emulates all events that should have 'happened' before the
 * specified timestmap. After an event is fired, it's removed from queue and freed.
 * @param[in] timestamp   the end timestamp.
 * @return                the time until the next event (in milliseconds) or 0 if the event
 *                        queue is empty.
 */
int scheduler_process(struct timeval* timestamp);


#endif

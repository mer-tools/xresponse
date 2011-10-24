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

#include "xhandler.h"
#include "xinput.h"
#include "xemu.h"
#include "xresponse.h"

#include "config.h"

/* environment variables used for xresponse configuration (optional) */
#define ENV_POINTER_INPUT_DEVICE     "XRESPONSE_POINTER_INPUT_DEVICE"
#define ENV_KEYBOARD_INPUT_DEVICE    "XRESPONSE_KEYBOARD_INPUT_DEVICE"


xhandler_t xhandler = {
		.damage_event_num = 0,
		.timestamp_atom = None,
		.display = NULL,
};

static const char* default_pointer_device = XINPUT_POINTER_DEVICE;
static const char* default_keyboard_device = XINPUT_KEYBOARD_DEVICE;

/* */
static int xhandler_xerror(Display* dpy, XErrorEvent *e)
{
	/* Really only here for debugging, for gdb backtrace */
	char msg[255];
	XGetErrorText(dpy, e->error_code, msg, sizeof msg);
	fprintf(stderr, "X error (%#lx): %s (opcode: %i:%i)", e->resourceid, msg, e->request_code, e->minor_code);

	/* ignore BadWindow errors - when monitoring application a window might
	 get destroyed before it's being monitored, which would result in BadWindow
	 error when xresponse tries to monitor it */
	if (e->error_code == BadWindow ||
	/* Unknown error, result of closing and opening a monitored application.
	 Ignore for now. TODO: find out what exactly the erorr means
	 */
	e->error_code == 151 ||
	/* BadDamage error sometimes happens when calling XDamageDestroy for
	 * windows being removed. Apparently the damage has been already destroyed
	 * before the call.*/
	e->error_code == 153 || e->error_code == 182) {
		fprintf(stderr, " - ignored\n");
		return 0;
	}
	exit(1);
}

/**
 * Opens the specified device type/name from the supplied input device list.
 * @param devInfo   the input device list.
 * @param count     the number of devices in the list.
 * @param type      the device type.
 * @param name      the device name (can be NULL).
 * @return          the opened xemu_device_t device.
 */
static xemu_device_t xhandler_open_device(XDeviceInfo *devInfo, int count, int type, const char* name)
{
	xemu_device_t device = {.dev = NULL, .naxis = 2};
	int i;

	for (i = 0; i < count; i++) {
		if (devInfo[i].use == type) {
			if (name == NULL || !strcmp(name, devInfo[i].name)) {
				device.dev = XOpenDevice(xhandler.display, devInfo[i].id);
				/* read number of axes supported by pointer devices */
//				if (type == IsXExtensionPointer)
				{
					XAnyClassPtr any = devInfo[i].inputclassinfo;
					int index = 0;
					for (index = 0; index < devInfo[i].num_classes; index++) {
						if (any->class == ValuatorClass) {
							XValuatorInfo* valuator = (XValuatorInfo*)any;
							if (device.naxis < valuator->num_axes) device.naxis = valuator->num_axes;
						}
						any = (XAnyClassPtr)((char*)any + any->length);
					}
					if (device.naxis > XEMU_POINTER_MAX_AXES) device.naxis = XEMU_POINTER_MAX_AXES;
					break;
				}
			}
		}
	}
	return device;
}


Time xhandler_get_server_time()
{
	XChangeProperty(xhandler.display, DefaultRootWindow(xhandler.display), xhandler.timestamp_atom, xhandler.timestamp_atom, 8, PropModeReplace,
			(unsigned char*) "a", 1);
	for (;;) {
		XEvent xevent;

		XMaskEvent(xhandler.display, PropertyChangeMask, &xevent);
		if (xevent.xproperty.atom == xhandler.timestamp_atom)
			return xevent.xproperty.time;
	}
}


bool xhandler_get_xevent_timed(XEvent *event_return, struct timeval *tv)
{
	if (options.abort_wait)
		return False;

	if (tv == NULL) {
		XNextEvent(xhandler.display, event_return);
		return True;
	}

	XFlush(xhandler.display);

	if (xrecord.display && XPending(xrecord.display)) {
		XRecordProcessReplies(xrecord.display);
	}
	if (XPending(xhandler.display) != 0) {
		XNextEvent(xhandler.display, event_return);
		return True;
	}

	while (True) {
		int fd = ConnectionNumber(xhandler.display);
		int fdrec = 0;
		fd_set readset;
		int maxfd = fd;
		int rc;

		FD_ZERO(&readset);
		FD_SET(fd, &readset);

		if (xrecord.display) {
			fdrec = ConnectionNumber(xrecord.display);
			FD_SET(fdrec, &readset);
			maxfd = fdrec > fd ? fdrec : fd;
		}

		rc = select(maxfd + 1, &readset, NULL, NULL, tv);

		if (rc <= 0) {
			return False;
		}

		if (xrecord.display && FD_ISSET(fdrec, &readset)) {
			XRecordProcessReplies(xrecord.display);
		}
		if (FD_ISSET(fd, &readset)) {
			XNextEvent(xhandler.display, event_return);
			return True;
		}
	}
}


bool xhandler_init(const char *dpy_name)
{
	int unused;
	int count, i;
	XDeviceInfo *devInfo;
	char* deviceName;

	if ((xhandler.display = XOpenDisplay(dpy_name)) == NULL) {
		fprintf(stderr, "Unable to connect to DISPLAY.\n");
		return false;
	}
	/* Check the extensions we need are available */

	if (!XTestQueryExtension(xhandler.display, &unused, &unused, &unused, &unused)) {
		fprintf(stderr, "No XTest extension found\n");
		return false;
	}

	if (!XDamageQueryExtension(xhandler.display, &xhandler.damage_event_num, &unused)) {
		fprintf(stderr, "No DAMAGE extension found\n");
		return false;
	}

	XSetErrorHandler(xhandler_xerror);

	/* Needed for get_server_time */
	xhandler.timestamp_atom = XInternAtom(xhandler.display, "_X_LATENCY_TIMESTAMP", False);
	XSelectInput(xhandler.display, DefaultRootWindow(xhandler.display), PropertyChangeMask | SubstructureNotifyMask);


	/* open input device required for XTestFakeDeviceXXX functions */
	if (!(devInfo = XListInputDevices(xhandler.display, &count)) || !count) {
		fprintf(stderr, "Cannot input list devices\n");
		return false;
	}

	/* By default the first extension device of appropriate type will be choosed.
	 It is possible to manually specify the input device with ENV_POINTER_INPUT_DEVICE and ENV_KEYBOARD_INPUT_DEVICE
	 environment variables */
	deviceName = getenv(ENV_POINTER_INPUT_DEVICE);
	if (!(deviceName && *deviceName))
		deviceName = default_pointer_device;
	fprintf(stderr, "Using pointer device: %s (%s)\n", deviceName, default_pointer_device);

	xemu.pointer = xhandler_open_device(devInfo, count, IsXExtensionPointer, deviceName);
	if (!xemu.pointer.dev) {
		fprintf(stderr, "Failed to open default device: %s. Opening the first pointer device\n", deviceName);
		xemu.pointer = xhandler_open_device(devInfo, count, IsXExtensionPointer, NULL);
	}

	deviceName = getenv(ENV_KEYBOARD_INPUT_DEVICE);
	if (!(deviceName && *deviceName))
		deviceName = default_keyboard_device;

	xemu.keyboard = xhandler_open_device(devInfo, count, IsXExtensionKeyboard, deviceName);
	if (!xemu.pointer.dev) {
		fprintf(stderr, "Failed to open default device: %s. Opening the first pointer device\n", deviceName);
		xemu.pointer = xhandler_open_device(devInfo, count, IsXExtensionKeyboard, NULL);
	}
	XFreeDeviceList(devInfo);
	return true;
}


/**
 * Eat all Damage events in the X event queue.
 */
void xhandler_eat_damage()
{
	while (XPending(xhandler.display)) {
		xevent_t e;

		XNextEvent(xhandler.display, &e.ev);

		if (e.ev.type == xhandler.damage_event_num + XDamageNotify) {
			XDamageSubtract(xhandler.display, e.dev.damage, None, None);
		}
	}
}


/**
 * Releases resources allocated by X event handler.
 */
void xhandler_fini()
{
	XCloseDisplay(xhandler.display);
}



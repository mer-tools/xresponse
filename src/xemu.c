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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "xemu.h"
#include "xhandler.h"
#include "scheduler.h"
#include "xresponse.h"

xemu_t xemu = {
		.keyboard = {.dev = NULL, .naxis = 2},
		.pointer = {.dev = NULL, .naxis = 2},
		.display = NULL,
};

// this maps what keysyms need a modifier pushed
static int keysym_to_modifier_map[ MAX_KEYSYM ];
static KeyCode keysym_to_keycode_map[ MAX_KEYSYM ];


/* Code below this comment has been copied from xautomation / vte.c and
 * modified minimally as required to co-operate with xresponse. */

/* All key events need to go through here because it handles the lookup of
 * keysyms like Num_Lock, etc.  Thing should be something that represents a
 * single key on the keyboard, like KP_PLUS or Num_Lock or just A */
static KeyCode thing_to_keycode(char *thing)
{
	KeyCode kc;
	KeySym ks;

	ks = XStringToKeysym(thing);
	if (ks == NoSymbol) {
		fprintf(stderr, "Unable to resolve keysym for '%s'\n", thing);
		return (thing_to_keycode("space"));
	}

	kc = XKeysymToKeycode(xemu.display, ks);
	return (kc);
}


/*
 * Public API implementation
 */

void xemu_init(Display* dpy)
{
	xemu.display = dpy;
}

void xemu_fini()
{
	if (xemu.keyboard.dev) XCloseDevice(xemu.display, xemu.keyboard.dev);
	if (xemu.pointer.dev) XCloseDevice(xemu.display, xemu.pointer.dev);
}


/* Simulate pressed key(s) to generate thing character
 * Only characters where the KeySym corresponds to the Unicode
 * character code and KeySym < MAX_KEYSYM are supported,
 * except the special character 'Tab'. */
Time xemu_send_string(char *thing_in)
{
	if (xemu.keyboard.dev) {
		KeyCode wrap_key;
		int i = 0;

		KeyCode keycode;
		KeySym keysym;
		Time start;

		wchar_t thing[CMD_STRING_MAXLEN];
		wchar_t wc_singlechar_str[2];
		wmemset(thing, L'\0', CMD_STRING_MAXLEN);
		mbstowcs(thing, thing_in, CMD_STRING_MAXLEN);
		wc_singlechar_str[1] = L'\0';

		xhandler_eat_damage(xemu.display);
		start = xhandler_get_server_time(xemu.display);

		while ((thing[i] != L'\0') && (i < CMD_STRING_MAXLEN)) {

			wc_singlechar_str[0] = thing[i];

			/* keysym = wchar value */
			keysym = wc_singlechar_str[0];

			/* Keyboard modifier and KeyCode lookup */
			wrap_key = keysym_to_modifier_map[keysym];
			keycode = keysym_to_keycode_map[keysym];

			if (keysym >= MAX_KEYSYM || !keycode) {
				fprintf(stderr, "Special character '%ls' is currently not supported.\n", wc_singlechar_str);
			} else {
				if (wrap_key) scheduler_add_event(SCHEDULER_EVENT_KEY, xemu.keyboard.dev, wrap_key, True, 0, 0);
				scheduler_add_event(SCHEDULER_EVENT_KEY, xemu.keyboard.dev, keycode, True, 0, 0);
				scheduler_add_event(SCHEDULER_EVENT_KEY, xemu.keyboard.dev, keycode, False, 0, 0);
				if (wrap_key) scheduler_add_event(SCHEDULER_EVENT_KEY, xemu.keyboard.dev, wrap_key, False, 0, 0);

				/* Not flushing after every key like we need to, thanks
				 * thorsten@staerk.de */
				XFlush(xemu.display);
			}

			i++;

		}
		return start;
	}
	return 0;
}

#define MAX_SHIFT_LEVELS		256

/* Load keycodes and modifiers of current keyboard mapping into arrays,
 * this is needed by the send_string function */
void xemu_load_keycodes()
{
	char *str;
	KeySym keysym;
	KeyCode keycode;
	KeyCode mask_to_modifier_map[MAX_SHIFT_LEVELS] = { 0 };
	int ikey, ishift, igroup;

	/* reset mapping tables */
	memset(keysym_to_modifier_map, 0, sizeof(keysym_to_modifier_map));
	memset(keysym_to_keycode_map, 0, sizeof(keysym_to_keycode_map));

	/* initialize [modifier mask: modifier keycode] mapping table */
	XModifierKeymap* modkeys = XGetModifierMapping(xemu.display);
	for (ishift = 0; ishift < 8 * modkeys->max_keypermod; ishift += modkeys->max_keypermod) {
		if (modkeys->modifiermap[ishift]) {
			mask_to_modifier_map[1 << (ishift / modkeys->max_keypermod)] = modkeys->modifiermap[ishift];
		}
	}
	XFreeModifiermap(modkeys);

	/* acquire keycode:keysyms mapping */
	XkbDescPtr xkb = XkbGetMap(xemu.display, XkbAllClientInfoMask, XkbUseCoreKbd);

	/* initialize [keysym:keycode] and [keysym:modifier keycode] tables */
	for (ikey = xkb->min_key_code; ikey <= xkb->max_key_code; ikey++) {
		XkbSymMapPtr mkeys = &xkb->map->key_sym_map[ikey];
		for (igroup = 0; igroup < XkbKeyNumGroups(xkb, ikey); igroup++) {
			XkbKeyTypePtr keytype = &xkb->map->types[mkeys->kt_index[igroup]];
			unsigned char levels[MAX_SHIFT_LEVELS] = { 0 };
			/*  make temporary [shift level: modifier mask] mapping */
			for (ishift = 0; ishift < keytype->map_count; ishift++) {
				if (!levels[keytype->map[ishift].level])
					levels[keytype->map[ishift].level] = keytype->map[ishift].mods.mask;
			}

			for (ishift = 0; ishift < keytype->num_levels; ishift++) {
				str = XKeysymToString(XkbKeySymsPtr(xkb, ikey)[ishift]);

				if (str != NULL) {
					keysym = XStringToKeysym(str);
					keycode = XKeysymToKeycode(xemu.display, keysym);

					if ((keysym < MAX_KEYSYM) && (!keysym_to_keycode_map[keysym])) {
						keysym_to_modifier_map[keysym] = mask_to_modifier_map[levels[ishift]];
						keysym_to_keycode_map[keysym] = keycode;
					}
				}
			}
		}
	}
	XkbFreeClientMap(xkb, XkbAllClientInfoMask, True);
}

Time xemu_send_key(char *thing, unsigned long delay)
{
	if (xemu.keyboard.dev) {
		Time start = xhandler_get_server_time(xemu.display);
		KeyCode kc = thing_to_keycode(thing);

		scheduler_add_event(SCHEDULER_EVENT_KEY, xemu.keyboard.dev, kc, True, 0, 0);
		scheduler_add_event(SCHEDULER_EVENT_KEY, xemu.keyboard.dev, kc, False, delay, 0);

		return start;
	}
	return 0;
}


/**
 * 'Fakes' a mouse click, returning time sent.
 */
Time xemu_button_event(int x, int y, int delay)
{
	if (xemu.pointer.dev) {
		Time start = xhandler_get_server_time(xemu.display);

		scheduler_add_event(SCHEDULER_EVENT_MOTION, xemu.pointer.dev, x, y, 0, xemu.pointer.naxis);
		scheduler_add_event(SCHEDULER_EVENT_BUTTON, xemu.pointer.dev, Button1, True, 0, xemu.pointer.naxis);
		scheduler_add_event(SCHEDULER_EVENT_BUTTON, xemu.pointer.dev, Button1, False, delay, 0);

		return start;
	}
	return 0;
}

Time xemu_drag_event(int x, int y, int button_state, int delay)
{
	if (xemu.pointer.dev) {
		Time start = xhandler_get_server_time(xemu.display);

		scheduler_add_event(SCHEDULER_EVENT_MOTION, xemu.pointer.dev, x, y, delay, xemu.pointer.naxis);

		if (button_state == XR_BUTTON_STATE_PRESS) {
			scheduler_add_event(SCHEDULER_EVENT_BUTTON, xemu.pointer.dev, Button1, True, 0, xemu.pointer.naxis);
		}
		if (button_state == XR_BUTTON_STATE_RELEASE) {
			scheduler_add_event(SCHEDULER_EVENT_BUTTON, xemu.pointer.dev, Button1, False, 0, 0);
		}
		return start;
	}
	return 0;
}



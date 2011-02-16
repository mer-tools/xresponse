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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/time.h>
#include <regex.h>
#include <sys/signal.h>
#include <limits.h>
#include <wchar.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/Xutil.h>
#include <X11/Xproto.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/record.h>

#include "xresponse.h"
#include "window.h"
#include "application.h"
#include "xemu.h"
#include "scheduler.h"
#include "xhandler.h"
#include "xinput.h"
#include "report.h"


/* 
 * defs
 */

#define DEFAULT_KEY_DELAY	(100lu)

#define DEFAULT_DRAG_DELAY	(20lu)

#define DEFAULT_DRAG_COUNT	(10u)

/* wait timer resolution (msecs) */
#define WAIT_RESOLUTION		100

#define streq(a,b)      (strcmp(a,b) == 0)


#define ASIZE(a)	(sizeof(a) / sizeof(a[0]))


/* region filtering rules */
enum {
	EXCLUDE_NONE, /* region filtering is not set */
	EXCLUDE_LESS, /* exclude regions that are less or equal than specified region/size */
	EXCLUDE_GREATER
/* exclude regions that are greater than specified region/size */
};


/* options data */
options_t options = {
	.damage_wait_secs = -1,
	.break_on_damage = 0,
	.abort_wait = 0,

	.exclude_size = 0,
	.exclude_rules = EXCLUDE_NONE,

	.break_timeout = 0,
};


bool check_timeval_timeout(struct timeval* tv1, struct timeval* tv2, int timeout)
{
	struct timeval tv = *tv1;
	tv.tv_usec += timeout * 1000;
	tv.tv_sec += tv.tv_usec / 1000000;
	tv.tv_usec %= 1000000;

	return tv2->tv_sec > tv.tv_sec || (tv2->tv_sec == tv.tv_sec && tv2->tv_usec >= tv.tv_usec);
}


/**
 * Checks if the damaged area from damage event matches exclusion filter.
 * @param[in] dev  the damage event.
 * @return         true if the area matches exclusion filter and must be excluded from reports.
 *                 false otherwise.
 */
static bool match_exclude_rules(XDamageNotifyEvent* dev)
{
	/* no exclude filters set, match everything */
	if (options.exclude_rules == EXCLUDE_NONE) return false;

	/* exclude areas that are less than the specified values */
	if (options.exclude_rules == EXCLUDE_LESS) {
		/* if exclude size is specified, check area total size */
		if (options.exclude_size) return (options.exclude_size >= dev->area.width * dev->area.height);
		/* otherwise check area edge size */
		return (dev->area.width <= options.exclude_rect.width || dev->area.height <= options.exclude_rect.height);
	}
	/* process EXCLUDE_GREATER rule */

	/* if exclude size is specified, check area total size */
	if (options.exclude_size) return (options.exclude_size < dev->area.width * dev->area.height);
	/* otherwise check area edge size */
	return (dev->area.width > options.exclude_rect.width || dev->area.height > options.exclude_rect.height);
}



/**
 * Retrieves and processes single X event.
 */
static int process_event()
{
	xevent_t e;
	struct timeval tv = { 0, WAIT_RESOLUTION * 1000 };

	if (!options.abort_wait && xhandler_get_xevent_timed(&e.ev, &tv)) {
		if (e.ev.type == xhandler.damage_event_num + XDamageNotify) {
			XDamageNotifyEvent *dev = &e.dev;
			int xpos = dev->area.x + dev->geometry.x;
			int ypos = dev->area.y + dev->geometry.y;
			/* check if the damage are is in the monitoring area */
			if (xpos + dev->area.width >= options.interested_damage_rect.x && xpos <= (options.interested_damage_rect.x
					+ options.interested_damage_rect.width) && ypos + dev->area.height >= options.interested_damage_rect.y &&
					ypos <= (options.interested_damage_rect.y + options.interested_damage_rect.height)) {
				if (!match_exclude_rules(dev)) {
					window_t* win = window_find(dev->drawable);
					report_add_message(dev->timestamp, "Got damage event %dx%d+%d+%d from 0x%lx (%s)\n", dev->area.width,
							dev->area.height, xpos, ypos, dev->drawable,
							win && win->application ? win->application->name : "unknown");

					if (response.last_action_time) {
						window_t* win = window_find(dev->drawable);
						if (win && win->application) {
							application_register_damage(win->application, dev);
						}
					}
				}
			}
			XDamageSubtract(xhandler.display, dev->damage, None, None);
		} else if (e.ev.type == CreateNotify) {
			/* check new windows if we have to monitor them */
			XCreateWindowEvent* ev = &e.cev;
			/* TODO: Check if we really must monitor only main windows.
			 Probably done to avoid double reporting. We could avoid that by
			 going through monitored window list, checking if this window
			 is in the parent chain of any monitored window. If so, remove the child.
			 Might be expensive though, but worth a try.
			 */
			if (ev->parent == DefaultRootWindow(xhandler.display)) {
				window_t* win = window_try_monitor(ev->window);
				if (win) {
					Time start = xhandler_get_server_time();
					report_add_message(start, "Created window 0x%lx (%s)\n", ev->window,
							win->application ? win->application->name : "unknown");
				}
			}
		} else if (e.ev.type == UnmapNotify) {
			XUnmapEvent* ev = &e.uev;
			window_t* win = window_find(ev->window);
			if (win) {
				Time start = xhandler_get_server_time();
				report_add_message(start, "Unmapped window 0x%lx (%s)\n", ev->window,
						win->application ? win->application->name : "unknown");
			}
		} else if (e.ev.type == MapNotify) {
			XMapEvent* ev = &e.mev;
			window_t* win = window_find(ev->window);
			if (win) {
				Time start = xhandler_get_server_time();
				report_add_message(start, "Mapped window 0x%lx (%s)\n", ev->window,
						win->application ? win->application->name : "unknown");
			}
		} else if (e.ev.type == DestroyNotify) {
			XDestroyWindowEvent* ev = (XDestroyWindowEvent*) &e.dstev;
			window_t* win = window_find(ev->window);
			if (win) {
				Time start = xhandler_get_server_time();
				report_add_message(start, "Destroyed window 0x%lx (%s)\n", ev->window,
						win->application ? win->application->name : "unknown");
				window_remove(win);
			}
		} else {
			/* remove to avoid reporting unwanted even types ?
			 with window creation monitoring there are more unhandled event types */
			/* fprintf(stderr, "Got unwanted event type %d\n", e.type); */
		}
		return e.ev.type;
	}
	return 0;
}

/** 
 * Waits for a damage 'response' to above click / keypress(es)
 */
static int wait_response()
{
	struct timeval current_time = { 0 }, last_time = { 0 }, start_time = { 0 }, report_time = { 0 };
	int event_type = 0;

	gettimeofday(&start_time, NULL);
	last_time = start_time;
	current_time = start_time;
	report_time = start_time;

	while (!options.abort_wait && (!options.damage_wait_secs || !check_timeval_timeout(&start_time, &current_time, options.damage_wait_secs * 1000))) {
		gettimeofday(&current_time, NULL);
		/* check if break timeout is specified and elapsed */
		if (options.break_timeout && check_timeval_timeout(&last_time, &current_time, options.break_timeout))
			break;

		/* simulate events */
		scheduler_process(&current_time);

		event_type = process_event();
		if (event_type == xhandler.damage_event_num + XDamageNotify) {
			last_time = current_time;

			/* check if options.break_on_damage was set and elapsed */
			if (options.break_on_damage && !(--options.break_on_damage))
				break;
		} else if (!event_type) {
			if (response.last_action_time) {
				if (check_timeval_timeout(&response.last_action_timestamp, &current_time, response.timeout)) {
					application_response_report();
				}
			}
			report_flush_queue();
			report_time = current_time;
		}
		if (check_timeval_timeout(&report_time, &current_time, REPORT_TIMEOUT)) {
			report_flush_queue();
			report_time = current_time;
		}
	}
	report_flush_queue();
	return 0;
}

/**
 * Retrieves and processes queued X events.
 */
static void process_events()
{
	while (process_event())
		;
}

void usage(char *progname)
{
	fprintf(stderr, "%s: usage, %s <-o|--logfile output> [commands..]\n"
		"Commands are any combination/order of;\n"
		"-c|--click <XxY[,delay]>            Send click and await damage response\n"
		"                                    Delay is in milliseconds.\n"
		"                                    If not specified no delay is used\n"
		"-d|--drag <delay|XxY,delay|XxY,...> Simulate mouse drag and collect damage\n"
		"                                    Optionally add delay between drag points\n"
		"-d <X1xY1-X2xY2[*delay[+count]]>    Simulate 'smooth' dragging between the specified\n"
		"                                    points by interpolating the <count> drag points\n"
		"                                    with <delay> between them. By default count is 10\n"
		"                                    and delay is 20 ms.\n"
		"-k|--key <keysym[,delay]>           Simulate pressing and releasing a key\n"
		"                                    Delay is in milliseconds.\n"
		"                                If not specified, default of %lu ms is used\n"
		"-m|--monitor <WIDTHxHEIGHT+X+Y>     Watch area for damage ( default fullscreen )\n"
		"-w|--wait <seconds>                 Max time to wait for damage, set to 0 to\n"
		"                                    monitor for ever.\n"
		"                                    ( default 5 secs)\n"
		"-s|--stamp <string>                 Write 'string' to log file\n"
		"-t|--type <string>                  Simulate typing a string\n"
		"-i|--inspect                        Just display damage events\n"
		"-id|--id <id>                       Resource id of window to examine\n"
		"-v|--verbose                        Output response to all command line options\n"
		"-a|--application <name>             Monitor windows related to the specified application.\n"
		"                                    Use '*' to monitor all applications.\n"
		"-x|--exclude <XxY|S[,less|greater]> Exclude regions from damage reports based on their size.\n"
		"                                    Specify either region dimensions XxY or size S.\n"
		"                                    less - exclude regions less or equal than specified (default).\n"
		"                                    greater - exclude regions graeter than specified.\n"
		"-b|--break <msec>|damage[,<number>] Break the wait if no damage was registered in <msec> period,\n"
		"                                    or after the <number> damage event if 'damage' was specified.\n"
		"-l|--level <raw|delta|box|nonempty> Specify the damage reporting level.\n"
		"-u|--user                           Enable user input monitoring.\n"
		"-U|--user-all                       Enable all user input monitoring, including pointer movement.\n"
"-r|--response <timeout[,verbose]>   Enable application response monitoring (timeout given in msecs).\n"
		"                                    If verbose is not specified the damage reporting will be suppresed.\n"
		"\n", progname, progname, DEFAULT_KEY_DELAY);
	exit(1);
}


static bool match_regex(const char* options, const char* pattern)
{
	regex_t reg;
	int rc;

	regcomp(&reg, pattern, REG_EXTENDED | REG_NOSUB);
	rc = regexec(&reg, options, 0, NULL, 0);
	regfree(&reg);
	return rc == 0;
}

/**
 * Aborts damage wait loop by setting AbortWait flag
 */
static void abort_wait()
{
	options.abort_wait = true;
}

/* Code copy from xautomation / vte.c ends */

int main(int argc, char **argv)
{
	int cnt, x, y, i = 0, verbose = 0;
	Window win = 0;
	Bool keysymMappingInitialized = False;
	int rc = 0;
	int inputEvents[100];
	int inputEventsIndex = 0;
	int iEvent = 0;

	if (argc == 1)
		usage(argv[0]);

	const char* log_file = NULL;
	if (streq(argv[1],"-o") || streq(argv[1],"--logfile")) {
		i++;

		if (++i > argc)
			usage(argv[0]);

		log_file = argv[i];
	}
	report_init(log_file);

	if (!xhandler_init(getenv("DISPLAY")))
		exit(1);

	report_add_message(xhandler_get_server_time(), "Startup\n");

	/* initialize subsystems */
	xemu_init(xhandler.display);
	scheduler_init(xhandler.display);
	window_init(xhandler.display);
	application_init();

	/*
	 * Process the command line options.
	 * Skip emulation options (--click, --drag, --key, --type), but remember they index
	 * and process them later.
	 */
	while (++i < argc) {

		if (streq(argv[i],"-v") || streq(argv[i],"--verbose")) {
			verbose = 1;
			continue;
		}

		if (streq(argv[i], "-id") || streq(argv[i], "--id")) {
			char name[PATH_MAX];
			if (++i >= argc)
				usage(argv[0]);

			cnt = sscanf(argv[i], "0x%lx", &win);
			if (cnt < 1) {
				cnt = sscanf(argv[i], "%lu", &win);
			}
			if (cnt < 1) {
				fprintf(stderr, "*** invalid window id '%s'\n", argv[i]);
				usage(argv[0]);
			}
			sprintf(name, "0x%lx", win);
			if (!window_add(win, application_monitor(name))) {
				fprintf(stderr, "Could not setup damage monitoring for window 0x%lx!\n", win);
				exit(1);
			}
			if (verbose)
				report_add_message(REPORT_LAST_TIMESTAMP, "Monitoring window 0x%lx\n", win);

			continue;
		}

		if (streq(argv[i], "-a") || streq(argv[i], "--application")) {
			if (++i >= argc)
				usage(argv[0]);

			response.application = application_monitor(argv[i]);
			if (response.application && verbose) {
				report_add_message(REPORT_LAST_TIMESTAMP, "Monitoring application '%s'\n", argv[i]);
			}
			if (!strcmp(argv[i], "*")) {
				application_set_monitor_all(true);
			}
			continue;
		}

		if (streq("-c", argv[i]) || streq("--click", argv[i])) {
			if (!xemu.pointer) {
				fprintf(stderr, "Failed to open pointer device, unable to simulate pointer events.\n");
				exit(-1);
			}
			if (inputEventsIndex == ASIZE(inputEvents)) {
				fprintf(stderr, "Too many input events specified\n");
				exit(-1);
			}
			if (!match_regex(argv[i + 1], "^[0-9]+x[0-9]+(,[0-9]+)?$")) {
				fprintf(stderr, "Failed to parse --c options: %s\n", argv[i + 1]);
				exit(-1);
			}
			inputEvents[inputEventsIndex++] = i;
			if (++i >= argc)
				usage(argv[0]);

			continue;
		}

		if (streq("-l", argv[i]) || streq("--level", argv[i])) {
			if (++i >= argc)
				usage(argv[0]);

			if (!strcmp(argv[i], "raw")) {
				window_set_damage_level(XDamageReportRawRectangles);
			} else if (!strcmp(argv[i], "delta")) {
				window_set_damage_level(XDamageReportDeltaRectangles);
			} else if (!strcmp(argv[i], "box")) {
				window_set_damage_level(XDamageReportDeltaRectangles);
			} else if (!strcmp(argv[i], "nonempty")) {
				window_set_damage_level(XDamageReportNonEmpty);
			} else {
				fprintf(stderr, "Unrecongnized damage level: %s\n", argv[i]);
				usage(argv[0]);
			}
			if (verbose)
				report_add_message(REPORT_LAST_TIMESTAMP, "Setting damage report level to %s\n", argv[i]);
			continue;
		}

		if (streq("-x", argv[i]) || streq("--exclude", argv[i])) {
			char* exclude[] = { "none", "less", "greater" };

			if (options.exclude_rules != EXCLUDE_NONE) {
				fprintf(stderr, "Duplicated --exclude parameter detected. Aborting\n");
				exit(-1);
			}

			if (++i >= argc)
				usage(argv[0]);
			char rules[32] = "";
			if ((cnt = sscanf(argv[i], "%ux%u,%s", &options.exclude_rect.width, &options.exclude_rect.height, rules)) >= 2) {
				options.exclude_size = 0;
			} else if ((cnt = sscanf(argv[i], "%u,%s", &options.exclude_size, rules)) >= 1) {
				options.exclude_rect.width = 0;
				options.exclude_rect.height = 0;
			} else {
				fprintf(stderr, "*** failed to parse '%s'\n", argv[i]);
				usage(argv[0]);
			}
			options.exclude_rules = *rules && !strcmp(rules, "greater") ? EXCLUDE_GREATER : EXCLUDE_LESS;
			if (verbose) {
				if (options.exclude_size) {
					report_add_message(REPORT_LAST_TIMESTAMP, "Excluding damage areas %s than %d pixels\n", exclude[options.exclude_rules],
							options.exclude_size);
				} else {
					report_add_message(REPORT_LAST_TIMESTAMP, "Excluding damage areas %s than (%dx%d)\n", exclude[options.exclude_rules],
							options.exclude_rect.width, options.exclude_rect.height);
				}
			}
			continue;
		}

		if (streq("-m", argv[i]) || streq("--monitor", argv[i])) {
			if (options.interested_damage_rect.width || options.interested_damage_rect.height || options.interested_damage_rect.x
					|| options.interested_damage_rect.y) {
				fprintf(stderr, "Duplicated --monitor parameter detected. Aborting\n");
				exit(-1);
			}
			if (++i >= argc)
				usage(argv[0]);

			if ((cnt = sscanf(argv[i], "%ux%u+%u+%u", &options.interested_damage_rect.width, &options.interested_damage_rect.height,
					&options.interested_damage_rect.x, &options.interested_damage_rect.y)) != 4) {
				fprintf(stderr, "*** failed to parse '%s'\n", argv[i]);
				usage(argv[0]);
			}
			if (verbose) {
				report_add_message(REPORT_LAST_TIMESTAMP, "Set monitor rect to %ix%i+%i+%i\n", options.interested_damage_rect.width,
						options.interested_damage_rect.height, options.interested_damage_rect.x, options.interested_damage_rect.y);
			}
			continue;
		}

		if (streq("-w", argv[i]) || streq("--wait", argv[i])) {
			if (++i >= argc)
				usage(argv[0]);

			if (options.damage_wait_secs >= 0) {
				fprintf(stderr, "Duplicate -w(--wait) option detected. Discarding the previous value\n");
			}
			if ((options.damage_wait_secs = atoi(argv[i])) < 0) {
				fprintf(stderr, "*** failed to parse '%s'\n", argv[i]);
				usage(argv[0]);
			}
			if (verbose)
				report_add_message(REPORT_LAST_TIMESTAMP, "Set event timeout to %isecs\n", options.damage_wait_secs);

			continue;
		}

		if (streq("-b", argv[i]) || streq("--break", argv[i])) {
			if (options.break_timeout || options.break_on_damage) {
				fprintf(stderr, "Duplicate -b(--break)option detected. Discarding the previous value\n");
				options.break_timeout = 0;
				options.break_on_damage = 0;
			}
			if (++i >= argc)
				usage(argv[0]);

			if (!strncmp(argv[i], "damage", 6)) {
				sscanf(argv[i] + 6, ",%d", &options.break_on_damage);
				if (!options.break_on_damage)
					options.break_on_damage = 1;
				if (verbose)
					report_add_message(REPORT_LAST_TIMESTAMP, "Break wait on the %d damage event\n", options.break_on_damage);
			} else {
				if ((options.break_timeout = atoi(argv[i])) < 0) {
					fprintf(stderr, "*** failed to parse '%s'\n", argv[i]);
					usage(argv[0]);
				}
				if (verbose)
					report_add_message(REPORT_LAST_TIMESTAMP, "Set break timout to %imsecs\n", options.break_timeout);
			}
			continue;
		}

		if (streq("-d", argv[i]) || streq("--drag", argv[i])) {
			if (!xemu.pointer) {
				fprintf(stderr, "Failed to open pointer device, unable to simulate pointer events.\n");
				exit(-1);
			}
			if (inputEventsIndex == ASIZE(inputEvents)) {
				fprintf(stderr, "Too many input events specified\n");
				exit(-1);
			}
			if (!match_regex(argv[i + 1], "^([0-9]+,)?(([0-9]+x[0-9]+,([0-9]+,)?)+[0-9]+x[0-9]+)$") &&
				 (!match_regex(argv[i + 1], "[0-9]+x[0-9]+-[0-9]+x[0-9]+") ||
				  !match_regex(argv[i + 1], "^(((([0-9]+,)?([0-9]+x[0-9]+)|([0-9]+x[0-9]+-[0-9]+x[0-9]+(\\*[0-9]+)?(\\+[1-9][0-9]*)?)),?)+)$") ) ) {
				fprintf(stderr, "Failed to parse --drag options: %s\n", argv[i + 1]);
				exit(-1);
			}
			inputEvents[inputEventsIndex++] = i;

			if (++i >= argc)
				usage(argv[0]);
			continue;
		}

		if (streq("-k", argv[i]) || streq("--key", argv[i])) {
			if (!xemu.keyboard) {
				fprintf(stderr, "Failed to open keyboard device, unable to simulate keyboard events.\n");
				exit(-1);
			}
			if (inputEventsIndex == ASIZE(inputEvents)) {
				fprintf(stderr, "Too many input events specified\n");
				exit(-1);
			}
			inputEvents[inputEventsIndex++] = i;
			if (++i >= argc)
				usage(argv[0]);

			continue;
		}

		if (streq("-t", argv[i]) || streq("--type", argv[i])) {
			if (!xemu.keyboard) {
				fprintf(stderr, "Failed to open keyboard device, unable to simulate keyboard events.\n");
				exit(-1);
			}
			if (inputEventsIndex == ASIZE(inputEvents)) {
				fprintf(stderr, "Too many input events specified\n");
				exit(-1);
			}
			inputEvents[inputEventsIndex++] = i;
			if (++i >= argc)
				usage(argv[0]);

			if (!keysymMappingInitialized) {
				xemu_load_keycodes();
				keysymMappingInitialized = True;
			}

			continue;
		}

		/* since moving from command sequence approach the inspect parameter is deprecated */
		if (streq("-i", argv[i]) || streq("--inspect", argv[i])) {
			if (verbose)
				report_add_message(REPORT_LAST_TIMESTAMP, "Just displaying damage events until timeout\n");
			continue;
		}

		/* */
		if (streq("-u", argv[i]) || streq("--user", argv[i]) ||
				(xrecord.motion = (streq("-U", argv[i]) || streq("--user-all", argv[i])) ) ) {
			xinput_init(xhandler.display);
			if (verbose)
				report_add_message(REPORT_LAST_TIMESTAMP, "Reporting user input events\n");

			continue;
		}

		if (streq(argv[i], "-r") || streq(argv[i], "--response")) {
			if (++i >= argc)
				usage(argv[0]);
			char option[500];
			cnt = sscanf(argv[i], "%u,%s", &response.timeout, option);
			if (cnt < 1) {
				fprintf(stderr, "*** invalid response timeout value '%s'\n", argv[i]);
				usage(argv[0]);
			}
			if (cnt < 2) {
				report_set_raw(true);
			} else {
				if (strcmp(option, "verbose")) {
					fprintf(stderr, "*** invalid response option '%s'\n", argv[i]);
					usage(argv[0]);
				}
			}
			application_monitor_screen();
			xinput_init(xhandler.display);
			if (verbose)
				report_add_message(REPORT_LAST_TIMESTAMP, "Monitoring application response time\n");

			continue;
		}

		fprintf(stderr, "*** Dont understand  %s\n", argv[i]);
		usage(argv[0]);
	}

	/* start monitoring the root window if no targets are specified */
	if ((window_empty() && application_empty()) || response.timeout) {
		application_monitor(ROOT_WINDOW_RESOURCE);
	}

	window_monitor_all();
	application_start_monitor();

	/* eat first damage event when options.break_on_damage set */
	if (options.break_on_damage)
		xhandler_eat_damage();

	/* monitor the whole screen of no area is specified */
	if (!options.interested_damage_rect.width && !options.interested_damage_rect.height && !options.interested_damage_rect.x
			&& !options.interested_damage_rect.y) {
		options.interested_damage_rect.x = 0;
		options.interested_damage_rect.y = 0;
		options.interested_damage_rect.width = DisplayWidth(xhandler.display, DefaultScreen(xhandler.display));
		options.interested_damage_rect.height = DisplayHeight(xhandler.display, DefaultScreen(xhandler.display));
	}

	/* emulate user input */

	for (iEvent = 0; iEvent < inputEventsIndex; iEvent++) {
		i = inputEvents[iEvent];

		if (!strcmp("-c", argv[i]) || !strcmp("--click", argv[i])) {
			unsigned long delay = 0;
			Time start = 0;
			cnt = sscanf(argv[++i], "%ux%u,%lu", &x, &y, &delay);
			if (cnt == 2) {
				start = xhandler_get_server_time();
				report_add_message(start, "Using no delay between press/release\n");
				delay = 0;
			} else if (cnt != 3) {
				fprintf(stderr, "cnt: %d\n", cnt);
				fprintf(stderr, "*** failed to parse '%s'\n", argv[i]);
				usage(argv[0]);
			}
			/* Send the event */
			process_events();
			start = xemu_button_event(x, y, delay);
			report_add_message(start, "Clicked %ix%i\n", x, y);

			continue;
		}

		if (!strcmp("-d", argv[i]) || !strcmp("--drag", argv[i])) {
			Time drag_time;
			char *s = NULL, *p = NULL;
			int button_state = XR_BUTTON_STATE_PRESS;

			s = p = argv[++i];
			int delay = DEFAULT_DRAG_DELAY;
			int x1, y1, x2, y2;
			while (p) {
				p = strchr(s, ',');
				if (p) {
					*p++ = '\0';
				}
				int count = DEFAULT_DRAG_COUNT;
				cnt = sscanf(s, "%ix%i-%ix%i*%i+%i", &x1, &y1, &x2, &y2, &delay, &count);
				fprintf(stderr, "cnt=%d\n", cnt);
				if (cnt >= 4) {
					drag_time = xemu_drag_event(x1, y1, button_state, delay);
					button_state = XR_BUTTON_STATE_NONE;
					report_add_message(drag_time, "Dragged to %ix%i\n", x1, y1);

					int xdev = (x2 - x1) / (count + 1);
					int ydev = (y2 - y1) / (count + 1);
					for (i = 1; i <= count; i++) {
						x = x1 + xdev * i;
						y = y1 + ydev * i;
						drag_time = xemu_drag_event(x, y, button_state, delay);
						report_add_message(drag_time, "Dragged to %ix%i\n", x, y);
					}
					if (!p) button_state = XR_BUTTON_STATE_RELEASE;
					drag_time = xemu_drag_event(x2, y2, button_state, delay);
					report_add_message(drag_time, "Dragged to %ix%i\n", x2, y2);
				}
				else if (cnt == 2) {
					/* Send the event */
					if (!p) {
						if (button_state == XR_BUTTON_STATE_PRESS) {
							fprintf(stderr, "*** Need at least 2 drag points!\n");
							usage(argv[0]);
						}
						button_state = XR_BUTTON_STATE_RELEASE;
					}
					drag_time = xemu_drag_event(x1, y1, button_state, delay);
					report_add_message(drag_time, "Dragged to %ix%i\n", x1, y1);

					/* Make sure button state set to none after first point */
					button_state = XR_BUTTON_STATE_NONE;

					/* reset the delay to default value */
					delay = DEFAULT_DRAG_DELAY;
				} else if (cnt == 1) {
					delay = x1;
				} else {
					fprintf(stderr, "*** failed to parse '%s'\n", argv[i]);
					usage(argv[0]);
				}
				s = p;
			}
			continue;
		}

		if (!strcmp("-k", argv[i]) || !strcmp("--key", argv[i])) {
			char *key = NULL;
			char separator;
			unsigned long delay = 0;
			Time start = 0;

			cnt = sscanf(argv[++i], "%a[^,]%c%lu", &key, &separator, &delay);
			if (cnt == 1) {
				report_add_message(REPORT_LAST_TIMESTAMP, "Using default delay between press/release\n", delay);
				delay = DEFAULT_KEY_DELAY;
			} else if (cnt != 3 || separator != ',') {
				fprintf(stderr, "cnt: %d\n", cnt);
				fprintf(stderr, "*** failed to parse '%s'\n", argv[i]);
				if (key != NULL)
					free(key);
				usage(argv[0]);
			}
			process_events();
			start = xemu_send_key(key, delay);
			report_add_message(start, "Simulating keypress/-release pair (keycode '%s')\n", key);
			free(key);

			continue;
		}

		if (!strcmp("-t", argv[i]) || !strcmp("--type", argv[i])) {
			Time start = xemu_send_string(argv[++i]);
			report_add_message(start, "Simulated keys for '%s'\n", argv[i]);

			continue;
		}

	}

	/* setting the default wait period */
	if (options.damage_wait_secs < 0) {
		options.damage_wait_secs = 5;
	}

	signal(SIGINT, abort_wait);
	/* wait for damage events */
	rc = wait_response();

	scheduler_fini();

	report_flush_queue();
	report_fini();
	xinput_fini();
	xemu_fini();

	window_fini();
	application_fini();

	xhandler_fini();


	return rc;
}

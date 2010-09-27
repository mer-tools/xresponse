/*
 * xresponse - Interaction latency tester,
 *
 * Written by Ross Burton & Matthew Allum  
 *              <info@openedhand.com> 
 *
 * Copyright (C) 2005 Nokia
 *
 * Licensed under the GPL v2 or greater.
 *
 * Window detection is based on code that is Copyright (C) 2007 Kim woelders.
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
 *
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

#include <wchar.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/Xutil.h>
#include <X11/Xproto.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/record.h>

#include "kbd.h"

/* 
 * defs
 */

#define streq(a,b)      (strcmp(a,b) == 0)
#define CMD_STRING_MAXLEN 256
typedef struct Rectangle { int x,y,width,height; } Rectangle;

#define ASIZE(a)	(sizeof(a) / sizeof(a[0]))

/* flags used for optional functionality requiring specific X extensions */
enum {
	XT_FALSE = 0,				
	XT_TRUE = 1,			
	XT_ERROR = -1,	
};

/* environment variables used for xresponse configuration (optional) */
#define ENV_POINTER_INPUT_DEVICE     "XRESPONSE_POINTER_INPUT_DEVICE"
#define ENV_KEYBOARD_INPUT_DEVICE    "XRESPONSE_KEYBOARD_INPUT_DEVICE"

/*
 * Global variables
 */

static FILE      *LogFile = NULL;       	/* The file to output the log output too */
static int        DamageEventNum;       	/* Damage Ext Event ID */
static Atom       AtomTimestamp;        	/* Atom for getting server time */
static Atom       AtomWmState = None;
static int        DamageWaitSecs = -1;   	/* Max time to collect damamge */
static Rectangle  InterestedDamageRect; 	/* Damage rect to monitor */
static Time       LastEventTime;      		/* When last last event was started */
static unsigned long DefaultDelay = 100; 	/* Default delay for key synthesis */
static int								DamageReportLevel = XDamageReportBoundingBox;
static int				BreakOnDamage = 0;			/* break on the specified damage event */
static int				EnableInputMonitoring = XT_FALSE;   /* mouse/keyboard input monitoring */
Display*					RecordDisplay = NULL; 	/* display connection for event recording */
XRecordContext 		RecordContext =  0;			/* XRecord context */

static Bool       SuppressReport = False;  	/* suppresses the standard event reports. Used with --response option */
static char				LastUserAction[256] = {0};	/* the last user action in user friendly format */

static Bool       AbortWait = False;      /* forces to abort damage wait loop if set to true */

/* response time support */
static unsigned int ResponseTimeout = 0; 					/* the timeout for application response checking */
static Time LastUserActionTime = 0; 							/* the last user user action timestamp */
static struct timeval LastUserActionLocalTime = {0};          /* the last user action local timestamp, used to measure response waiting time */

/* input devices used for faking input events */
XDevice* XKeyboardDevice      = NULL;
XDevice* XPointerDevice       = NULL;

/* region filtering rules */
enum {
	EXCLUDE_NONE,				/* region filtering is not set */
	EXCLUDE_LESS,				/* exclude regions that are less or equal than specified region/size */
	EXCLUDE_GREATER			/* exclude regions that are greater than specified region/size */
};

static Rectangle 		ExcludeRect;									/* Damage rect for filtering rules */
static unsigned int ExcludeSize = -1;							/* Damage rect size for filtering rules */
static unsigned int ExcludeRules = EXCLUDE_NONE;	/* filtering rules */


enum { /* for 'dragging' */
  XR_BUTTON_STATE_NONE,
  XR_BUTTON_STATE_PRESS,
  XR_BUTTON_STATE_RELEASE
};

/* wait timer resolution (msecs) */
#define WAIT_RESOLUTION		100

static unsigned int BreakTimeout = 0;

/* function declarations */
static Time get_server_time(Display *dpy);
static void eat_damage(Display *dpy);

/**
 * Checks if the diffrence between two timestamps is greater or equal
 * than the specified timeout in milliseconds.
 *
 * @param tv1[in]   the first timestamp (start value).
 * @param tv2[in]   the second timestamp (end value).
 * @param timeout   the timeout to check (in milliseconds).
 * @return          true if the difference between tiemestamps are greater or equal than specified.
 */
static Bool
check_timeval_timeout(struct timeval* tv1, struct timeval* tv2, int timeout) {
  struct timeval tv = *tv1;
  tv.tv_usec += timeout * 1000;
  tv.tv_sec += tv.tv_usec / 1000000;
  tv.tv_usec %= 1000000;

  return tv2->tv_sec > tv.tv_sec || (tv2->tv_sec == tv.tv_sec && tv2->tv_usec >= tv.tv_usec);
}


/*
 * logger implementation
 *
 * xresponse internally queues all reports until no damage events are received during
 * waiting times slice (WAIT_RESOLUTION define). After it the reports are sorted by
 * their timestamps and written to standard output.
 */

/*
 * Defines the maximum time period in milliseconds for report queueing.
 */
#define LOGGER_TIMEOUT          5000

/*
 * logger data structure
 */
typedef struct _log_record_t {
	Time timestamp;
	char* message;
	struct _log_record_t* next;
} log_record_t;

/*
 * logger indices
 */
static log_record_t LOG_TAIL = {0};
static log_record_t* logTail = &LOG_TAIL;
static log_record_t* logHead = &LOG_TAIL;

/**
 * Adds log message to logger
 *
 * This function allocates new log record, sets the specified timestamp,
 * formats the message and adds to the logger message queue.
 * @param timestamp[in]     the message timestamp.
 * @param format[in]        the message format (see printf specification).
 * @param ...               the message parameters.
 * @return
 */
static void
log_add_message(Time timestamp, const char* format, ...) {
	log_record_t* rec;

	rec = malloc(sizeof(log_record_t));
	if (rec) {
    va_list     ap;
    va_start(ap,format);
    if (vasprintf(&rec->message, format, ap) == -1) {
			free(rec);
			rec = NULL;
    }
	}
	if (rec) {
		rec->next = NULL;
		rec->timestamp = timestamp;

		logHead->next = rec;
		logHead = rec;
	}
	else {
		fprintf(stderr, "Warning, failed to buffer log record, out of memory\n");
		exit(-1);
	}
}

/**
 * Releases a previously allocated log record.
 *
 * @param rec[in]   the log record to release.
 * @return
 */
static void
log_free_message(log_record_t* rec) {
	free(rec->message);
	free(rec);
}

/**
 * Writes logger message to the standard output.
 *
 * @param timestamp[in]   the message timestamp.
 * @param mesasge[in]     the message  text.
 * @return
 */
static void
log_write_message(Time timestamp, const char* message) {
  static int  displayed_header;

	if (!SuppressReport) {
		if (!displayed_header) { 		/* Header */
			fprintf(LogFile, "\n"
				" Server Time : Diff    : Info\n"
				"-----------------------------\n");
			displayed_header = 1;
		}
    fprintf(LogFile, "%10lums : %5lums : %s", timestamp, timestamp - LastEventTime, message);
    LastEventTime = timestamp;
	}
	else {
		if (!timestamp) fprintf(LogFile, message);
	}
}

/**
 * Writes the next message to standard output.
 *
 * This function takes the next message after the specified, writes its contents
 * to standard ouput, removes the message record from logger queue and releases it.
 * @param next[in]    the previous message of the one to write.
 * @return
 */
static void
log_write_next_message(log_record_t* next) {
	log_record_t* rec = next->next;
	if (rec) {
		log_write_message(rec->timestamp, rec->message);
		next->next = rec->next;
		if (logHead == rec) logHead = next;
		log_free_message(rec);
	}
}


/**
 * Write queued messages to standard output and clears the logger
 * message queue
 *
 * @return
 */
static void
log_write_messages() {
	log_record_t* writeHead = logHead;

	while (logTail->next != writeHead->next) {
		log_record_t* rec = logTail->next;
		log_record_t* oldest = logTail;
		Time timestamp = logTail->next->timestamp;
		while (rec->next != writeHead->next) {
			if (timestamp > rec->next->timestamp) {
				timestamp = rec->next->timestamp;
				oldest = rec;
			}
			rec = rec->next;
		}
		log_write_next_message(oldest);
	}
  fflush(LogFile);
}

/**
 * Clears the logger message queue.
 * @return
 */

static void
log_clear() {
	log_record_t* rec;
	while (logTail->next) {
		rec = logTail->next;
		logTail->next = logTail->next->next;
		log_free_message(rec);
	}
	logHead = logTail;
}


/*
 * Event scheduler implementation.
 *
 * Events are simulation consists of two parts:
 * 1) when command line is parsed, the necessary user input events are queued into
 *    event scheduler
 * 2) during the main wait loop scheduler checks if the current time exceeds the
 *    specified event delay and fakes the event if necessary.
 *
 * Practically all user actions consist of multiple events. For example clicking at
 * the specified location produces 3 events - moving cursor, pressing button and
 * releasing button.
 */

enum {
  SCHEDULER_EVENT_NONE = 0,
  SCHEDULER_EVENT_BUTTON,
  SCHEDULER_EVENT_KEY,
  SCHEDULER_EVENT_MOTION
};

/**
 * Scheduler event structure
 */
typedef struct _scheduler_event_t {
  int type;               /* the event type */
  XDevice* device;        /* the target device (keyboard/mouse) */
  int param1;             /* first parameter. button/key or x coordinate */
  int param2;             /* second parameter. True/False (down/up) or y coordinate */
  int delay;              /* the event delay since the last event (in milliseconds) */
  struct _scheduler_event_t* next; /* the next scheduler event */
} scheduler_event_t;

static scheduler_event_t SCHEDULER_TAIL = {0};
scheduler_event_t* SchedulerHead = &SCHEDULER_TAIL;
scheduler_event_t* SchedulerTail = &SCHEDULER_TAIL;

/**
 * Adds new event to the scheduler.
 *
 * @param type[in]    the event type (SCHEDULER_EVENT_*)
 * @param device[in]  the input device.
 * @param param1[in]  the first parameter. For button and key events it's the button/key code.
 *                    For motion events it's the x coordinate of the new cursor location.
 * @param param2[in]  the second parameter. For button and key events its True/False indicating
 *                    if the button/key was pressed or released. For motion events it's the y
 *                    coordinate of the new cursor location.
 * @param delay[in]   the delay time since the last event (in milliseconds)
 * @return            the added event or NULL in the case of an error.
 */
scheduler_event_t*
scheduler_add_event(int type, XDevice* device, int param1, int param2, int delay) {
  scheduler_event_t* event = malloc(sizeof(scheduler_event_t));
  if (event) {
    event->type = type;
    event->device = device;
    event->param1 = param1;
    event->param2 = param2;
    event->delay = delay;
    event->next = NULL;
    SchedulerTail->next = event;
    SchedulerTail = event;
  }
  return event;
}


/**
 * Simulates user input event.
 *
 * @param dpy[in]     the display connection.
 * @param event[in]   the event to simulate.
 * @return
 */
void
scheduler_fake_event(Display* dpy, scheduler_event_t* event) {
  static int xPos = 0;
  static int yPos = 0;

  switch (event->type) {
    case SCHEDULER_EVENT_BUTTON: {
      int axis[2] = {xPos, yPos};
      XTestFakeDeviceButtonEvent(dpy, event->device, event->param1, event->param2, axis, 2, CurrentTime);
      break;
    }

    case SCHEDULER_EVENT_KEY:
      XTestFakeDeviceKeyEvent(dpy, event->device, event->param1, event->param2, NULL, 0, CurrentTime );
      break;

    case SCHEDULER_EVENT_MOTION:
      xPos = event->param1;
      yPos = event->param2;
    {
      int axis[2] = {xPos, yPos};
      XTestFakeDeviceMotionEvent(dpy, event->device, False, 0, axis, 2, CurrentTime);
      break;
    }
  }
}


/**
 * Processes events until the specified timestamp.
 *
 * This function simulates events that should have 'happened' before the
 * specified timestmap. After event is simulated, it will be removed from queue and its resources
 * freed.
 * @param dpy[in]         the display connection.
 * @param timestamp[in]   the timestamp
 * @return
 */
void
scheduler_process(Display* dpy, struct timeval* timestamp) {
  static struct timeval lastEventTime = {0};
  if (!lastEventTime.tv_sec) {
    lastEventTime = *timestamp;
  }

  while (SchedulerHead->next && check_timeval_timeout(&lastEventTime, timestamp, SchedulerHead->next->delay) ) {
    scheduler_event_t* event = SchedulerHead->next;
    SchedulerHead->next = event->next;
    scheduler_fake_event(dpy, event);
    if (SchedulerTail == event) SchedulerTail = &SCHEDULER_TAIL;
    free(event);
    lastEventTime = *timestamp;
  }
}

/**
 * Clears scheduler queue.
 *
 * @return
 */
void
scheduler_clear(void) {
  while (SchedulerHead->next) {
    scheduler_event_t* event = SchedulerHead->next;
    SchedulerHead->next = event->next;
    if (SchedulerTail == event) SchedulerTail = &SCHEDULER_TAIL;
    free(event);
  }
}


/* dynamic application/window monitoring */
#define MAX_APPLICATION_NAME	32
#define MAX_APPLICATIONS			50

/* resource alias for the root window */
#define ROOT_WINDOW_RESOURCE	"*SCREEN*"
#define RESPONSE_APPLICATION_NAME "*APPLICATION*"

/* flag specifying if all applications must be monitored */
Bool MonitorAllApplications = False;

/**
 * Application data structure
 */
typedef struct _application_t {
	
	char name[MAX_APPLICATION_NAME];				/* name of the application resource(binary) file */
	XDamageNotifyEvent firstDamageEvent; 		/* the first damage event after user action */
	XDamageNotifyEvent lastDamageEvent;			/* the last damage event after user action */
  int ref;
	
} application_t;

application_t* Applications[MAX_APPLICATIONS];  	/* application list */
int ApplicationIndex = 0;												/* first free index in application list */

application_t* ResponseApplication = NULL;
application_t* ScreenApplication = NULL;

#define MAX_WINDOWS			100

/**
 * Window data structure
 */
typedef struct _window_t {
	Window window;								/* the window id */
	Damage damage;								/* the damage context */
  application_t* application;   /* the owner appliation */
} window_t;

window_t Windows[MAX_WINDOWS];	/* monitored window list */
int WindowIndex = 0;						/* first free index in monitored window list */




static application_t* application_find(const char* name);
static application_t* application_monitor(const char* name);
static void application_remove(application_t* app);
static void application_release(application_t* app);
static void application_addref(application_t* app);

/**
 *
 * Window related functionality
 *
 */


/**
 * Adds window to monitored window list.
 * 
 * @param window[in] 	the window being monitored.
 * @return						a reference to the window data structure.
 */
static window_t* 
window_add(Window window, application_t* application) {
	if (WindowIndex <= MAX_WINDOWS) {
		window_t* win = &Windows[WindowIndex++];
		win->window = window;
		win->damage = 0;
		win->application = application;
		return win;
	}
	fprintf(stderr, "[window_add] Failed to add window (exceeding limit of %d windows)\n", MAX_WINDOWS);
	return NULL;
}

/**
 * Clears the monitored window list.
 * 
 * @return
 */
static void 
window_remove_all(Display* dpy) {
	window_t* win = NULL;
	for (win = Windows; win - Windows < WindowIndex; win++) {
		if (win->damage) XDamageDestroy(dpy, win->damage);
	}
	WindowIndex = 0;
}

/**
 * Searches monitored window list for the specified window.
 *
 * @param window[in]		the window to search.
 * @return							a reference to the located window or NULL otherwise.
 */
static window_t* 
window_find(Window window) {
	window_t* win = NULL;
	for (win = Windows; win - Windows < WindowIndex; win++) {
		if (window == win->window) return win;
	}
	return NULL;
}


/** 
 * Removes window from monitoring list.
 * 
 * @param window[in]		the window to remove.
 * @return
 */
static void 
window_remove(window_t* win, Display* dpy) {
	if (win) {
		application_t* app = win->application;
		window_t* tail = &Windows[--WindowIndex];
		XDamageDestroy(dpy, win->damage);
		if (win != tail) *win = *tail;
		application_release(app);
	}
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

/** 
 * Retrieves resource (application) name associated with the window.
 *
 * The root windows is aliased as '*'.
 * @param window[in]		the window which resource name should be retrieved.
 * @param dpy[in]				the default display.
 * @return							a reference to the resource name or NULL. This name will be changed
 *                      by another window_get_resource_name call, so don't store the reference
 *                      but copy contents if needed.
 */
static const char* 
window_get_resource_name(Window window, Display* dpy) {
	static char resourceName[MAX_APPLICATION_NAME];
	XClassHint classhint;
	
	/* alias the root window to '*' resource */
	if (window == DefaultRootWindow(dpy)) {
		strcpy(resourceName, ROOT_WINDOW_RESOURCE);
		return resourceName;
	}
	if(XGetClassHint(dpy, window, &classhint)) {
		strncpy(resourceName, classhint.res_name, MAX_APPLICATION_NAME);
		resourceName[MAX_APPLICATION_NAME - 1] = '\0';
		XFree(classhint.res_name);
		XFree(classhint.res_class);
		return resourceName;
	}
	return NULL;
}


/**
 * Start monitoring the specified window.
 *
 * @param app[in]				the owner application (can be NULL for windows monitored with --id option).
 * @param dpy[in]				the default display.
 * @return							true if the window monitoring started successfully.
 */
static Bool 
window_start_monitoring(window_t* win, Display* dpy) {
	win->damage = XDamageCreate(dpy, win->window, DamageReportLevel);
	if (!win->damage) {
		fprintf(stderr, "XDamageCreate failed for window %lx, application %s\n", win->window, win->application ? win->application->name : "(null)");
		return False;
	}
	if (win->window != DefaultRootWindow(dpy)) XSelectInput(dpy, win->window, StructureNotifyMask);	
	return True;
}

/**
 * Try to monitor the specified window.
 * 
 * This method checks if the specified window must/can be monitored and 
 * will start monitoring it.
 * @param window[in]			the window to monitor.
 * @param dpy[in]					the default display.
 * @return								a reference to the window data structure if monitoring 
 *                        started successfully. NULL otherwise.
 */
static window_t* 
window_try_monitoring(Window window, Display* dpy) {
	if (!window_find(window)) {
		const char* resource = window_get_resource_name(window, dpy);
		if (resource) {
			application_t* app = NULL;
      app = MonitorAllApplications ? application_monitor(resource) : application_find(resource);
			if (app) {
				window_t* win = window_add(window, app);
				if (win && window_start_monitoring(win, dpy)) {
					return win;
				}
				window_remove(win, dpy);
			}
		}
	}
	return NULL;
}


/**
 * Start to monitor all windows in list.
 *
 * This function is called in the begining, to start monitoring all windows
 * which was added manually with --id option.
 * @param dpy[in]		the default display.
 * @return 
 */
static void 
window_monitor_all(Display* dpy) {
	window_t* win = Windows;
	while (win - Windows < WindowIndex) {
		if (window_start_monitoring(win, dpy)) {
			win++;
		}
		else {
			window_remove(win, dpy);
		}
	}
}

/**
 * Tries to monitor children of the specified window.
 * 
 * This function will try to monitor every child of the specified window.
 * If monitoring a child fails, it will try to monitor its children.
 * @param window[in] 	the window which children to monitor.
 * @param dpy[in]			the default display.
 * @return
 */
static void 
window_try_monitoring_children(Window window, Display* dpy) {
	Window root_win, parent_win;
	Window *child_list;
	unsigned int num_children;
	int iWin;
	
	if (!XQueryTree(dpy, window, &root_win, &parent_win, &child_list, &num_children)) {
		fprintf(stderr, "Cannot query window tree (%lx)\n", window);
		return;
	}
	for (iWin = 0; iWin < num_children; iWin++) {	
		if (!window_try_monitoring(child_list[iWin], dpy)) {
			window_try_monitoring_children(child_list[iWin], dpy);
		}
	}
	if (child_list) XFree(child_list);
}

/**
 *
 * Application related functionality
 */

/**
 * Resets application damage events.
 * 
 * @param app[in]		the application.
 */
static void 
application_reset_events(application_t* app) {
	app->firstDamageEvent.timestamp = 0;
	app->lastDamageEvent.timestamp = 0;
}


/** 
 * Resets all application damage events.
 *
 * @return
 */
static void 
application_all_reset_events() {
	application_t** app = NULL;
	for (app = Applications; app - Applications < ApplicationIndex; app++) {
    if ((*app)->firstDamageEvent.timestamp) {
      application_reset_events(*app);
      application_release(*app);
    }
  }
  application_release(ResponseApplication);
}

/**
 * Adds a new application to application list.
 *
 * When a new window is created xresponse checks application list if it should start 
 * monitoring the created window.
 * @param name[in] 				the application name.
 * @param isDynamic[in]		specifies if application is added dynamically and can be removed
 *                        when all of its windows are destroyed.
 * @return								a reference to the application data structure.
 */
static application_t* 
application_add(const char* name)  {
	if (ApplicationIndex <= MAX_APPLICATIONS) {
		application_t* app = malloc(sizeof(application_t));
		if (app) {
			Applications[ApplicationIndex++] = app;
			strncpy(app->name, name, MAX_APPLICATION_NAME);
			app->name[MAX_APPLICATION_NAME - 1] = '\0';
			app->ref = 0;
      application_addref(app);
			application_reset_events(app);
			return app;
		}
		fprintf(stderr, "[application_add] failed to allocated memory for the application\n");
		return NULL;
	}
	fprintf(stderr, "[application_add] Failed to add application (exceeding limit of %d applications)\n", MAX_APPLICATIONS);
	return NULL;
}


/**
 * Increases application reference counter.
 *
 * @param app[in]   the application.
 * @return
 */
static void
application_addref(application_t* app) {
  if (app) {
    app->ref++;
  }
}


/**
 * Decrements application reference counter and removes the application from monitored
 * applications list if necessary.
 *
 * @param app[in]   the application to release.
 * @return
 */
static void 
application_release(application_t* app) {
  if (app) {
    if (! --app->ref) {
      application_remove(app);
    }
  }
}


/**
 * Adds application to monitored applications list if necessary and increments its
 * reference counter.
 *
 * @param name[in]    the application name.
 * @return            a reference to the added application.
 */
static application_t* 
application_monitor(const char* name) {
  application_t* app = application_find(name);
  if (!app) {
    app = application_add(name);
  }
  return app;
}


/**
 * Clears the application list.
 *
 * @return
 */
static void 
application_remove_all() {
	application_t** app = NULL;
	for (app = Applications; app - Applications < ApplicationIndex; app++) {
		free(*app);
	}
	ApplicationIndex = 0;
  ScreenApplication = NULL;
  ResponseApplication = NULL;
}

/**
 * Searches application list for the specified application.
 *
 * This function will increase application reference counter before returning it.
 * @param name[in]		the application name.
 * @return 						a reference to the located application or NULL otherwise.
 */
static application_t* 
application_find(const char* name) {
	application_t** app = NULL;
  
	for (app = Applications; app - Applications < ApplicationIndex; app++) {
		if (!strncmp(name, (*app)->name, MAX_APPLICATION_NAME)) {
      application_addref(*app);
      return *app;
    }
	}
	return NULL;
}

/**
 * Removes application from the list.
 *
 * @param name[in] 		the application name.
 * @return
 */
static void 
application_remove(application_t* app) {
  application_t** papp = NULL;
  --ApplicationIndex;
  for (papp = Applications; papp - Applications < ApplicationIndex; papp++) { 
    if (*papp == app) {
      *papp = Applications[ApplicationIndex];
      break;
    }
  }
  free(app);
}


/**
 * Log the response times.
 * 
 * @param logTimestamp    the log record timestamp.
 * @param appName         the application name.
 * @param firstTimestamp  timestamp of the first damage event.
 * @param lastTimestamp   timestamp of the last damage event.
 * @return
 */
static void 
log_response_data(Time logTimestamp, const char* appName, Time firstTimestamp, Time lastTimestamp) {
	log_add_message(logTimestamp, "\t%32s updates: first %5ims, last %5ims\n", appName,
											firstTimestamp - LastUserActionTime,  lastTimestamp - LastUserActionTime);
}  

/**
 * Logs the response data of applications.
 *
 * @param timestamp		the log event timestamp.
 * @return
 */
 static void 
application_log_response_data(Time timestamp) {
  application_t** app = NULL;
 
  if (ResponseApplication) {
    if (!ResponseApplication->firstDamageEvent.timestamp) {
      fprintf(stderr, "Warning, during the response timeout the monitored application %s did not receive any damage events."
                      "Using screen damage events instead.\n", ResponseApplication->name);
      if (ScreenApplication && ScreenApplication->firstDamageEvent.timestamp) {
        ResponseApplication->firstDamageEvent.timestamp = ScreenApplication->firstDamageEvent.timestamp;
        ResponseApplication->lastDamageEvent.timestamp = ScreenApplication->lastDamageEvent.timestamp;
        application_addref(ResponseApplication);
      }
    }
  }
  
  /* report response times for all 'damaged' applications */
	for (app = Applications; app - Applications < ApplicationIndex; app++) {
		if ((*app)->firstDamageEvent.timestamp) {
      log_response_data(timestamp, (*app)->name ? (*app)->name : "(unknown)", (*app)->firstDamageEvent.timestamp,  (*app)->lastDamageEvent.timestamp);
			(*app)->firstDamageEvent.timestamp = 0;
      application_release(*app);
		}
	}
  log_add_message(timestamp, "\n");
  application_release(ResponseApplication);
}


/**
 * Starts monitoring all windows belonging to the applications in list.
 *
 * @param dpy[in]		the default display.
 * @return
 */
static void 
application_monitor_all(Display* dpy) {
	Window win = DefaultRootWindow(dpy);
	window_try_monitoring(win, dpy);
	window_try_monitoring_children(win, dpy);
}


/**
 * Stores last user action description.
 *
 * The last user action description is used for reporting application response times
 * @param format[in]
 * @param ...
 */
static void 
register_user_action(const char* format, ...) {
	va_list ap;
  va_start(ap, format);
  vsnprintf(LastUserAction, sizeof(LastUserAction), format, ap);
  va_end(ap);
}


/** 
 * Reports application response times
 *
 * @return
 */
static void 
report_response_times() {
  if (LastUserActionTime) {
    log_add_message(0, "Device response time to %s:\n", LastUserAction);
    application_log_response_data(0);
    LastUserActionTime = 0;
    LastUserActionLocalTime.tv_sec = 0;
    LastUserActionLocalTime.tv_usec = 0;
  }
}

/* */
static int 
handle_xerror(Display *dpy, XErrorEvent *e)
{
  /* Really only here for debugging, for gdb backtrace */
  char msg[255];
  XGetErrorText(dpy, e->error_code, msg, sizeof msg);
  fprintf(stderr, "X error (%#lx): %s (opcode: %i:%i)",
				e->resourceid, msg, e->request_code, e->minor_code);

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
		  e->error_code == 153) {
			fprintf(stderr, " - ignored\n");
				return 0;
		}
	fprintf(stderr, "\n");
  exit(1);
}


/**
 * Get the current timestamp from the X server.
 */
static Time 
get_server_time(Display *dpy) 
{
  XChangeProperty (dpy, DefaultRootWindow (dpy), 
		   AtomTimestamp, AtomTimestamp, 8, 
		   PropModeReplace, (unsigned char*)"a", 1);
  for (;;) {
		XEvent xevent;

		XMaskEvent (dpy, PropertyChangeMask, &xevent);
		if (xevent.xproperty.atom == AtomTimestamp)	return xevent.xproperty.time;
    }
}

/**  
 * Get an X event with a timeout ( in secs ). The timeout is
 * updated for the number of secs left.
 */
static Bool
get_xevent_timed(Display        *dpy, XEvent         *event_return, struct timeval *tv) {

  if (AbortWait) return False;

  if (tv == NULL) {
		XNextEvent(dpy, event_return);
		return True;
	}

  XFlush(dpy);

  if (RecordDisplay && XPending(RecordDisplay)) {
    XRecordProcessReplies(RecordDisplay);
  }
  if (XPending(dpy) != 0) {
    XNextEvent(dpy, event_return);
    return True;
  }

  while (True) {
		int fd = ConnectionNumber(dpy);
		int fdrec = 0;
		fd_set readset;
		int maxfd = fd;
		int rc;

		FD_ZERO(&readset);
		FD_SET(fd, &readset);

		if (RecordDisplay) {
      fdrec = ConnectionNumber(RecordDisplay);
      FD_SET(fdrec, &readset);
      maxfd = fdrec > fd ? fdrec : fd;
    }

		rc = select(maxfd + 1, &readset, NULL, NULL, tv);

		if (rc <= 0) {
			return False;
		}

		if (RecordDisplay && FD_ISSET(fdrec, &readset)) {
		  XRecordProcessReplies(RecordDisplay);
		}
		if (FD_ISSET(fd, &readset)) {
		  XNextEvent(dpy, event_return);
		  return True;
		}
  }
}

/** 
 * Set up Display connection, required extensions and req other X bits
 */
static Display*
setup_display(char *dpy_name) 
{
  Display *dpy;
  int unused;
  int count, i;
  XDeviceInfo *devInfo;
  char* deviceName;

  if ((dpy = XOpenDisplay(dpy_name)) == NULL)
    {
      fprintf (stderr, "Unable to connect to DISPLAY.\n");
      return NULL;
    }

  /* Check the extensions we need are available */

  if (!XTestQueryExtension (dpy, &unused, &unused, &unused, &unused)) {
    fprintf (stderr, "No XTest extension found\n");
    return NULL;
  }

  if (!XDamageQueryExtension (dpy, &DamageEventNum, &unused)) {
    fprintf (stderr, "No DAMAGE extension found\n");
    return NULL;
  }
	
	{
		int major = 0, minor = 0;
		if (!XRecordQueryVersion(dpy, &major, &minor)) {
			fprintf(stderr, "No Record extension found\n");
			EnableInputMonitoring = XT_ERROR;
		}
	}

  XSetErrorHandler(handle_xerror); 

  /* Needed for get_server_time */
  AtomTimestamp = XInternAtom (dpy, "_X_LATENCY_TIMESTAMP", False);  
  XSelectInput(dpy, DefaultRootWindow(dpy), PropertyChangeMask | SubstructureNotifyMask);

  /* Needed for client window checking */
  AtomWmState = XInternAtom(dpy, "WM_STATE", False);

  /* open input device required for XTestFakeDeviceXXX functions */
  if (!(devInfo = XListInputDevices(dpy, &count)) || !count) {
    fprintf(stderr, "Cannot input list devices\n");
    return NULL;
  }

  /* By default the first extension device of appropriate type will be choosed.
     It is possible to manually specify the input device with ENV_POINTER_INPUT_DEVICE and ENV_KEYBOARD_INPUT_DEVICE
     environment variables */
  deviceName = getenv(ENV_POINTER_INPUT_DEVICE);
  if ( deviceName && ! *deviceName ) deviceName = NULL;
  for (i = 0; i < count; i++) {
    if ( (deviceName == NULL && devInfo[i].use == IsXExtensionPointer) || (deviceName && !strcmp(deviceName, devInfo[i].name) ) )  {
      XPointerDevice = XOpenDevice(dpy, devInfo[i].id);
      break;
    }
  }
  
  deviceName = getenv(ENV_KEYBOARD_INPUT_DEVICE);
  if ( deviceName && ! *deviceName ) deviceName = NULL;
  for (i = 0; i < count; i++) {
    if ( (deviceName == NULL && devInfo[i].use == IsXExtensionKeyboard) || (deviceName && !strcmp(deviceName, devInfo[i].name) ) )  {
      XKeyboardDevice = XOpenDevice(dpy, devInfo[i].id);
      break;
    }
  }
  XFreeDeviceList(devInfo);
  
  return dpy;
}


/**
 * Eat all Damage events in the X event queue.
 */
static void 
eat_damage(Display *dpy)
{
  while (XPending(dpy)) 
    {
      XEvent              xev;
      XDamageNotifyEvent *dev;

      XNextEvent(dpy, &xev);

      if (xev.type == DamageEventNum + XDamageNotify) 
	{
	  dev = (XDamageNotifyEvent*)&xev;
	  XDamageSubtract(dpy, dev->damage, None, None);
	}
    }
}

/** 
 * 'Fakes' a mouse click, returning time sent.
 */
static Time
fake_event(Display *dpy, int x, int y, int delay)
{
  if (XPointerDevice) {
    Time start = get_server_time(dpy);

    scheduler_add_event(SCHEDULER_EVENT_MOTION, XPointerDevice, x, y, 0);
    scheduler_add_event(SCHEDULER_EVENT_BUTTON, XPointerDevice, Button1, True, 0);
    scheduler_add_event(SCHEDULER_EVENT_BUTTON, XPointerDevice, Button1, False, delay);

    return start;
  }
  return 0;
}

static Time
drag_event(Display *dpy, int x, int y, int button_state, int delay)
{
  if (XPointerDevice) {
    Time start = get_server_time(dpy);

    scheduler_add_event(SCHEDULER_EVENT_MOTION, XPointerDevice, x, y, delay);

    if (button_state == XR_BUTTON_STATE_PRESS) {
      scheduler_add_event(SCHEDULER_EVENT_BUTTON, XPointerDevice, Button1, True, 0);
    }
    if (button_state == XR_BUTTON_STATE_RELEASE) {
      scheduler_add_event(SCHEDULER_EVENT_BUTTON, XPointerDevice, Button1, False, 0);
    }
    return start;
  }
  return 0;
}

/**
 * Retrieves and processes single X event.
 */
static int
process_event(Display* dpy) {
  XEvent e;
  struct timeval tv = {0, WAIT_RESOLUTION * 1000};
  
  if (!AbortWait && get_xevent_timed(dpy, &e, &tv)) {
    if (e.type == DamageEventNum + XDamageNotify) {
      XDamageNotifyEvent *dev = (XDamageNotifyEvent*)&e;
      int xpos = dev->area.x + dev->geometry.x;
      int ypos = dev->area.y + dev->geometry.y;
      /* check if the damage are is in the monitoring area */
      if (xpos + dev->area.width >= InterestedDamageRect.x &&
          xpos <= (InterestedDamageRect.x +	InterestedDamageRect.width) &&
          ypos + dev->area.height >= InterestedDamageRect.y &&
          ypos <= (InterestedDamageRect.y + InterestedDamageRect.height)) {
        /* check if the damage area satisfies filtering rules */
        if (ExcludeRules == EXCLUDE_NONE ||
            (ExcludeRules == EXCLUDE_LESS && (
                      (ExcludeSize && ExcludeSize < dev->area.width * dev->area.height) || 
                      (!ExcludeSize && (dev->area.width > ExcludeRect.width || dev->area.height > ExcludeRect.height)) ) ) ||
            (ExcludeRules == EXCLUDE_GREATER && (
                      (ExcludeSize &&	ExcludeSize >= dev->area.width * dev->area.height) ||
                      (!ExcludeSize && (dev->area.width <= ExcludeRect.width || dev->area.height <= ExcludeRect.height)) ) )  ){
          window_t* win = window_find(dev->drawable);
          
          log_add_message(dev->timestamp, "Got damage event %dx%d+%d+%d from 0x%lx (%s)\n",
                              dev->area.width, dev->area.height, xpos, ypos, dev->drawable,
                              win && win->application ? win->application->name : "unknown");

          if (LastUserActionTime) {
            window_t* win = window_find(dev->drawable);
            if (win && win->application) {
              if (win->application->firstDamageEvent.timestamp < LastUserActionTime) {
                win->application->firstDamageEvent = *dev;
                application_addref(win->application);
              }
              win->application->lastDamageEvent = *dev;
            }
          }
        }
      }
      XDamageSubtract(dpy, dev->damage, None, None);
    } 
    else if (e.type == CreateNotify) {
      /* check new windows if we have to monitor them */
      XCreateWindowEvent* ev = (XCreateWindowEvent*)&e;
      /* TODO: Check if we really must monitor only main windows.
         Probably done to avoid double reporting. We could avoid that by 
         going through monitored window list, checking if this window
         is in the parent chain of any monitored window. If so, remove the child.
         Might be expensive though, but worth a try.
      */
      if (ev->parent == DefaultRootWindow(dpy)) {
        window_t* win = window_try_monitoring(ev->window, dpy);
        if (win) {
          Time start = get_server_time(dpy);
          log_add_message(start, "Created window 0x%lx (%s)\n", ev->window, win->application ? win->application->name : "unknown");
        }
      }
    }
    else if (e.type == UnmapNotify) {
      XUnmapEvent* ev = (XUnmapEvent*)&e;
      window_t* win = window_find(ev->window);
      if (win) {
        Time start = get_server_time(dpy);
        log_add_message(start, "Unmapped window 0x%lx (%s)\n", ev->window, win->application ? win->application->name : "unknown");
      }
    }
    else if (e.type == MapNotify) {
      XMapEvent* ev = (XMapEvent*)&e;
      window_t* win = window_find(ev->window);
      if (win) {
        Time start = get_server_time(dpy);
        log_add_message(start, "Mapped window 0x%lx (%s)\n", ev->window, win->application ? win->application->name : "unknown");
      }
    }
    else if (e.type == DestroyNotify) {
      XDestroyWindowEvent* ev = (XDestroyWindowEvent*)&e;
      window_t* win = window_find(ev->window);
      if (win) {
        Time start = get_server_time(dpy);
        log_add_message(start, "Destroyed window 0x%lx (%s)\n", ev->window, win->application ? win->application->name : "unknown");
        window_remove(win, dpy);
      }
    }
    else {
      /* remove to avoid reporting unwanted even types ?
         with window creation monitoring there are more unhandled event types */
      /* fprintf(stderr, "Got unwanted event type %d\n", e.type); */
    }
    return e.type;
  }
  log_write_messages();
  return 0;
}

/** 
 * Waits for a damage 'response' to above click / keypress(es)
 */
static int
wait_response(Display *dpy)
{
  struct timeval currentTime = {0}, lastEvent = {0}, startTime = {0}, reportTime = {0};
  int eventType = 0;
  
  gettimeofday(&startTime, NULL);
  lastEvent = startTime;
  currentTime = startTime;
  reportTime = startTime;
  
	while (!AbortWait && (!DamageWaitSecs || !check_timeval_timeout(&startTime, &currentTime, DamageWaitSecs * 1000)) ) {
    gettimeofday(&currentTime, NULL);
    /* check if break timeout is specified and elapsed */
		if (BreakTimeout && check_timeval_timeout(&lastEvent, &currentTime, BreakTimeout)) break;

	  /* simulate events */
    scheduler_process(dpy, &currentTime);

    eventType = process_event(dpy);
    if (eventType == DamageEventNum + XDamageNotify) {
      lastEvent = currentTime;

      if (check_timeval_timeout(&reportTime, &currentTime, LOGGER_TIMEOUT)) {
        log_write_messages();
        reportTime = currentTime;
      }
      /* check if BreakOnDamage was set and elapsed */
      if (BreakOnDamage && !(--BreakOnDamage)) break;
    }
    else if (!eventType) {
      if (LastUserActionTime) {
        if (check_timeval_timeout(&LastUserActionLocalTime, &currentTime, ResponseTimeout)) {
          report_response_times();
        }
      }

      reportTime = currentTime;
    }      
 }
  return 0;
}

/**
 * Retrieves and processes queued X events.
 */
static void
process_events(Display* dpy) {
  while (process_event(dpy));
}

void
usage(char *progname)
{
  fprintf(stderr, "%s: usage, %s <-o|--logfile output> [commands..]\n" 
	          "Commands are any combination/order of;\n"
	          "-c|--click <XxY[,delay]>            Send click and await damage response\n" 
	          "                                    Delay is in milliseconds.\n"
						"                                    If not specified no delay is used\n"
	          "-d|--drag <delay|XxY,delay|XxY,...> Simulate mouse drag and collect damage\n"
	          "                                    Optionally add delay between drag points\n"
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
						"-r|--response <timeout[,verbose]>   Enable application response monitoring (timeout given in msecs).\n"
						"                                    If verbose is not specified the damage reporting will be suppresed.\n"
						"\n",
	  progname, progname, DefaultDelay);
  exit(1);
}

/* Code below this comment has been copied from xautomation / vte.c and
 * modified minimally as required to co-operate with xresponse. */

/* All key events need to go through here because it handles the lookup of
 * keysyms like Num_Lock, etc.  Thing should be something that represents a
 * single key on the keyboard, like KP_PLUS or Num_Lock or just A */
static KeyCode 
thing_to_keycode(Display *dpy, char *thing ) 
{
  KeyCode kc;
  KeySym ks;
  
  ks = XStringToKeysym(thing);
  if (ks == NoSymbol){
    fprintf(stderr, "Unable to resolve keysym for '%s'\n", thing);
    return(thing_to_keycode(dpy, "space" ));
  }

  kc = XKeysymToKeycode(dpy, ks);
  return(kc);
}

/* Simulate pressed key(s) to generate thing character
 * Only characters where the KeySym corresponds to the Unicode
 * character code and KeySym < MAX_KEYSYM are supported,
 * except the special character 'Tab'. */
static Time send_string( Display *dpy, char *thing_in ) 
{
  if (XKeyboardDevice) {
    KeyCode wrap_key;
    int i = 0;

    KeyCode keycode;
    KeySym keysym;
    Time start;

    wchar_t thing[ CMD_STRING_MAXLEN ];
    wchar_t wc_singlechar_str[2];
    wmemset( thing, L'\0', CMD_STRING_MAXLEN );
    mbstowcs( thing, thing_in, CMD_STRING_MAXLEN );
    wc_singlechar_str[ 1 ] = L'\0';

    eat_damage(dpy);
    start = get_server_time(dpy);

    while( ( thing[ i ] != L'\0' ) && ( i < CMD_STRING_MAXLEN ) ) {

      wc_singlechar_str[ 0 ] = thing[ i ];

      /* keysym = wchar value */
      keysym = wc_singlechar_str[ 0 ];
      
      /* Keyboard modifier and KeyCode lookup */
      wrap_key = keysym_to_modifier_map[ keysym ];
      keycode = keysym_to_keycode_map[keysym];

      if( keysym >= MAX_KEYSYM || !keycode) {
        fprintf( stderr, "Special character '%ls' is currently not supported.\n", wc_singlechar_str );
      }
      else {
        if( wrap_key ) scheduler_add_event(SCHEDULER_EVENT_KEY, XKeyboardDevice, wrap_key, True, 0);
        scheduler_add_event(SCHEDULER_EVENT_KEY, XKeyboardDevice, keycode, True, 0);
        scheduler_add_event(SCHEDULER_EVENT_KEY, XKeyboardDevice, keycode, False, 0);
        if( wrap_key ) scheduler_add_event(SCHEDULER_EVENT_KEY, XKeyboardDevice, wrap_key, False, 0);

        /* Not flushing after every key like we need to, thanks
         * thorsten@staerk.de */
        XFlush( dpy );
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
static void 
load_keycodes( Display *dpy ) 
{
  char *str;
  KeySym keysym;
  KeyCode keycode;
	KeyCode mask_to_modifier_map[MAX_SHIFT_LEVELS] = {0};
	int ikey, ishift, igroup;

	/* reset mapping tables */
	memset(keysym_to_modifier_map, 0, sizeof(keysym_to_modifier_map));
	memset(keysym_to_keycode_map, 0, sizeof(keysym_to_keycode_map));
	
	/* initialize [modifier mask: modifier keycode] mapping table */
	XModifierKeymap* modkeys = XGetModifierMapping(dpy);
	for (ishift = 0; ishift < 8 * modkeys->max_keypermod; ishift += modkeys->max_keypermod) {
		if (modkeys->modifiermap[ishift]) {
			mask_to_modifier_map[1 << (ishift / modkeys->max_keypermod)] = modkeys->modifiermap[ishift];
		}
	}
	XFreeModifiermap(modkeys);
	
	/* acquire keycode:keysyms mapping */
	XkbDescPtr xkb = XkbGetMap(dpy, XkbAllClientInfoMask, XkbUseCoreKbd);
	
	/* initialize [keysym:keycode] and [keysym:modifier keycode] tables */
	for (ikey = xkb->min_key_code; ikey <= xkb->max_key_code; ikey++) {
		XkbSymMapPtr mkeys = &xkb->map->key_sym_map[ikey];
		for (igroup = 0; igroup < XkbKeyNumGroups(xkb, ikey); igroup++) {
			XkbKeyTypePtr keytype = &xkb->map->types[mkeys->kt_index[igroup]];
			unsigned char levels[MAX_SHIFT_LEVELS] = {0};
			/*  make temporary [shift level: modifier mask] mapping */
			for (ishift = 0; ishift < keytype->map_count; ishift++) {
				if (!levels[keytype->map[ishift].level]) levels[keytype->map[ishift].level] = keytype->map[ishift].mods.mask;
			}
			
			for (ishift = 0; ishift < keytype->num_levels; ishift++) {
				str = XKeysymToString(XkbKeySymsPtr(xkb, ikey)[ishift]);
				
				if( str != NULL ) {
					keysym = XStringToKeysym( str );
					keycode = XKeysymToKeycode( dpy, keysym );

					if( ( keysym < MAX_KEYSYM ) && ( !keysym_to_keycode_map[ keysym ]) ) {
						keysym_to_modifier_map[ keysym ] = mask_to_modifier_map[levels[ishift]];
						keysym_to_keycode_map[ keysym ] = keycode;
					}
				}
			}
		}
	}
  XkbFreeClientMap(xkb, XkbAllClientInfoMask, True);
}


static Time 
send_key(Display *dpy, char *thing, unsigned long delay) 
{
  if (XKeyboardDevice) {
    Time start = get_server_time(dpy);
    KeyCode kc = thing_to_keycode( dpy, thing );

    scheduler_add_event(SCHEDULER_EVENT_KEY, XKeyboardDevice, kc, True, 0);
    scheduler_add_event(SCHEDULER_EVENT_KEY, XKeyboardDevice, kc, False, delay);

    return start;
  }
  return 0;
}


static Bool
window_has_property(Display * dpy, Window win, Atom atom) {
  Atom type_ret;
  int format_ret;
  unsigned char *prop_ret;
  unsigned long bytes_after, num_ret;

  type_ret = None;
  prop_ret = NULL;
  XGetWindowProperty(dpy, win, atom, 0, 0, False, AnyPropertyType,
                     &type_ret, &format_ret, &num_ret,
                     &bytes_after, &prop_ret);
  if (prop_ret)
    XFree(prop_ret);

  return (type_ret != None) ? True : False;
}

/*
 * Check if window is viewable
 */
static Bool
window_is_viewable(Display * dpy, Window win) {
  Bool ok;
  XWindowAttributes xwa;

  XGetWindowAttributes(dpy, win, &xwa);

  ok = (xwa.class == InputOutput) && (xwa.map_state == IsViewable);

  return ok;
}


/**
 * Finds client window for window reported at the cursor location.
 */
static Window 
window_find_client(Display* dpy, Window window) {
  Window root, parent, win = None;
  Window *children;
  unsigned int n_children;
  int i;

  if (!XQueryTree(dpy, window, &root, &parent, &children, &n_children))
     return None;
  if (!children)
     return None;

  /* Check each child for WM_STATE and other validity */
  for (i = (int) n_children - 1; i >= 0; i--) {
    if (!window_is_viewable(dpy, children[i])) {
      children[i] = None; /* Don't bother descending into this one */
      continue;
    }
    if (!window_has_property(dpy, children[i], AtomWmState)) {
      continue;
    }

      /* Got one */
    win = children[i];
    break;
  }

  /* No children matched, now descend into each child */
  if (win == None) {
    for (i = (int) n_children - 1; i >= 0; i--) {
      if (children[i] == None)
        continue;
      win = window_find_client(dpy, children[i]);
      if (win != None)
        break;
    }
  }
  XFree(children);

  return win;
}

/**
 * Finds client window at the cursor location.
 *
 */
static Window 
get_window_at_cursor(Display* dpy) {
  Window root, child, client;
  int root_x, root_y, win_x, win_y;
  unsigned mask;

  XQueryPointer(dpy, DefaultRootWindow(dpy), &root, &child, &root_x, &root_y, &win_x, &win_y, &mask);
 
  if (!AtomWmState) return child;

  client = window_find_client(dpy, child);
  return client == None ? child : client;
}

void reset_application_response_monitoring(Time timestamp) {
	if (ResponseTimeout) {
		if (LastUserActionTime) {
			log_add_message(0, "Warning, new user event received in the middle of update. "
															 "It is possible that update time is not correct.\n");
			application_all_reset_events();
		}
		LastUserActionTime = timestamp;
    gettimeofday(&LastUserActionLocalTime, NULL);
	}
}


void record_callback(XPointer closure, XRecordInterceptData* data) {
	if (XRecordFromServer == data->category) {
		Display* dpy = (Display*)closure;
		xEvent* xev = (xEvent*)data->data;
		int type = xev->u.u.type;
		static int x,y;
    window_t* win;
    char extInfo[256] = "";
				
		switch (type) {
			case ButtonPress:
 				win = window_find(get_window_at_cursor(dpy));
        if (win) {
          sprintf(extInfo, "(%s)", win->application->name);
        } 
        log_add_message(xev->u.keyButtonPointer.time, "Button %x pressed at %dx%d %s\n", xev->u.u.detail, x, y, extInfo);
        if (ResponseTimeout) {
          register_user_action("press (%dx%d) %s", x, y, extInfo);
          reset_application_response_monitoring(xev->u.keyButtonPointer.time);

          if (MonitorAllApplications) {
            ResponseApplication = win ? win->application : NULL;
          }
          
          /* Increasing response application reference counter for both - press and release events */
          application_addref(ResponseApplication);
          application_addref(ResponseApplication);
        }
        break;
			
			case ButtonRelease:
        /* report any button press related response times */
        report_response_times();
      
        /**/
 				win = window_find(get_window_at_cursor(dpy));
        if (win) {
          sprintf(extInfo, "(%s)", win->application->name);
        }
				log_add_message(xev->u.keyButtonPointer.time, "Button %x released at %dx%d %s\n", xev->u.u.detail, x, y, extInfo);
        if (ResponseTimeout) {
          register_user_action("release (%dx%d) %s", x, y, extInfo);
          reset_application_response_monitoring(xev->u.keyButtonPointer.time);
        }
				break;
			
			case KeyPress:
				log_add_message(xev->u.keyButtonPointer.time, "Key %s pressed\n",
											XKeysymToString(XKeycodeToKeysym(dpy, xev->u.u.detail, 0)));
				break;
				
			case KeyRelease:
				log_add_message(xev->u.keyButtonPointer.time, "Key %s released\n",
											XKeysymToString(XKeycodeToKeysym(dpy, xev->u.u.detail, 0)));
				break;
			
			case MotionNotify:
				/*
				log_add_message(xev->u.keyButtonPointer.time, "Pointer moved to %dx%d\n",
											xev->u.keyButtonPointer.rootX, xev->u.keyButtonPointer.rootY);
				LastEventTime = xev->u.keyButtonPointer.time;
				*/
				x = xev->u.keyButtonPointer.rootX;
				y = xev->u.keyButtonPointer.rootY;
				break;
			
			default:
				fprintf(stderr, "Unknown device event type %d\n", type);
				break;
		}
		
	}
	
	XRecordFreeData(data);
}

static void start_user_input_monitoring(Display* dpy) {
	if (EnableInputMonitoring == XT_ERROR) {
		fprintf(stderr, "Can't monitor user input wihout xrecord extension\n");
		exit(-1);
	}
	
	if (EnableInputMonitoring == XT_FALSE) {
		XRecordClientSpec clients = XRecordAllClients;
		int numRanges = 3;
		int iRange = 0;
		XRecordRange** recRange = 0;
		
		RecordDisplay = XOpenDisplay(getenv("DISPLAY"));
		if (!RecordDisplay) {
			fprintf(stderr, "Failed to open event recording display connection\n");
			exit(-1);
		}
		/* prepare event range data */
		recRange = malloc(sizeof(XRecordRange*) * numRanges);
		if (!recRange) {
			fprintf(stderr, "Failed to allocate record range array\n");
			exit(-1);
		}
		XRecordRange* range;
		XRecordRange** ptrRange = recRange;

		range = XRecordAllocRange();
		range->device_events.first = KeyPress;
		range->device_events.last = KeyPress;
		*ptrRange++ = range;
		
		range = XRecordAllocRange();
		range->device_events.first = ButtonPress;
		range->device_events.last = ButtonRelease;
		*ptrRange++ = range;

		range = XRecordAllocRange();
		range->device_events.first = MotionNotify;
		range->device_events.last = MotionNotify;
		*ptrRange++ = range;
		
		/* */
		RecordContext = XRecordCreateContext(dpy, XRecordFromServerTime, &clients, 1, recRange, numRanges);
		XFlush(dpy);
		XFlush(RecordDisplay);
		XRecordEnableContextAsync(RecordDisplay, RecordContext, record_callback, (XPointer)dpy);

		/* release range data */
		for (iRange = 0; iRange < numRanges; iRange++)	{
			free(recRange[iRange]);
		}
		free(recRange);
	
		EnableInputMonitoring = XT_TRUE;
	}
}

static Bool match_regex(const char* options, const char* pattern) {
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
void abort_wait() {
  AbortWait = True;
}


/* Code copy from xautomation / vte.c ends */


int 
main(int argc, char **argv) 
{
  Display *dpy = NULL;
	int      cnt, x, y, i = 0, verbose = 0;
  Window win = 0;
	Bool keysymMappingInitialized = False;
	int rc = 0;
	int inputEvents[100];
	int inputEventsIndex = 0;
	int iEvent = 0;

  if (argc == 1)
    usage(argv[0]);

  if (streq(argv[1],"-o") || streq(argv[1],"--logfile"))
    {
      i++;

      if (++i > argc) usage (argv[0]);

      if ((LogFile = fopen(argv[i], "w")) == NULL)
	fprintf(stderr, "Failed to create logfile '%s'\n", argv[i]);
    }

  if (LogFile == NULL) 
    LogFile = stdout;

  if ((dpy = setup_display(getenv("DISPLAY"))) == NULL)
    exit(1);

	LastEventTime = get_server_time(dpy);
	log_add_message(LastEventTime, "Startup\n");
	
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
			char name[MAX_APPLICATION_NAME];
			if (++i>=argc) usage (argv[0]);
	  
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
				fprintf(stderr, "Could not setup damage monitoring for window 0x%lx!\n",	win);
				exit(1);
			}
			if (verbose) log_add_message(LastEventTime, "Monitoring window 0x%lx\n", win);

			continue;
		}
	
		if (streq(argv[i], "-a") || streq(argv[i], "--application")) {
			if (++i >= argc) usage (argv[0]);
			
      ResponseApplication = application_monitor(argv[i]);
			if (ResponseApplication && verbose) {
				log_add_message(LastEventTime, "Monitoring application '%s'\n", argv[i]);
			}
			if (!strcmp(argv[i], "*")) {
				MonitorAllApplications = True;
			}
			continue;
		}
	
		if (streq("-c", argv[i]) || streq("--click", argv[i])) {
      if (!XPointerDevice) {
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
			if (++i>=argc) usage (argv[0]);
	  
			continue;
		}

		if (streq("-l", argv[i]) || streq("--level", argv[i])) {
			if (++i>=argc) usage (argv[0]);
			
			if (!strcmp(argv[i], "raw")) {
				DamageReportLevel = XDamageReportRawRectangles;
			}
			else if (!strcmp(argv[i], "delta")) {
				DamageReportLevel = XDamageReportDeltaRectangles;
			}
			else if (!strcmp(argv[i], "box")) {
				DamageReportLevel = XDamageReportDeltaRectangles;
			}
			else if (!strcmp(argv[i], "nonempty")) {
				DamageReportLevel = XDamageReportNonEmpty;
			}
			else {
				fprintf(stderr, "Unrecongnized damage level: %s\n", argv[i]);
				usage(argv[0]);
			}
			if (verbose) log_add_message(LastEventTime, "Setting damage report level to %s\n", argv[i]);
			continue;
		}
		
		if (streq("-x", argv[i]) || streq("--exclude", argv[i])) {
			char* exclude[] = {"none", "less", "greater"};
			
			if (ExcludeRules != EXCLUDE_NONE) {
				fprintf(stderr, "Duplicated --exclude parameter detected. Aborting\n");
				exit(-1);
			}
			
			if (++i>=argc) usage (argv[0]);
			char rules[32] = "";
			if ((cnt = sscanf(argv[i], "%ux%u,%s", &ExcludeRect.width, &ExcludeRect.height, rules)) >= 2) {
				ExcludeSize = 0;
			}
			else if ((cnt = sscanf(argv[i], "%u,%s", &ExcludeSize, rules)) >= 1) {
				ExcludeRect.width = 0;
				ExcludeRect.height = 0;
			}
			else {
				fprintf(stderr, "*** failed to parse '%s'\n", argv[i]);
				usage(argv[0]);
			}
			ExcludeRules = *rules && !strcmp(rules, "greater") ? EXCLUDE_GREATER : EXCLUDE_LESS;
			if (verbose) {
				if (ExcludeSize) {
					log_add_message(LastEventTime, "Excluding damage areas %s than %d pixels\n", exclude[ExcludeRules], ExcludeSize);
				}
				else {
					log_add_message(LastEventTime, "Excluding damage areas %s than (%dx%d)\n", exclude[ExcludeRules], ExcludeRect.width, ExcludeRect.height);
				}
			}
			continue;
		}
      
		if (streq("-m", argv[i]) || streq("--monitor", argv[i])) {
			if (InterestedDamageRect.width || InterestedDamageRect.height ||
							InterestedDamageRect.x || InterestedDamageRect.y) {
				fprintf(stderr, "Duplicated --monitor parameter detected. Aborting\n");
				exit(-1);
			}
			if (++i>=argc) usage (argv[0]);
				
			if ((cnt = sscanf(argv[i], "%ux%u+%u+%u", 
						&InterestedDamageRect.width,
						&InterestedDamageRect.height,
						&InterestedDamageRect.x,
						&InterestedDamageRect.y)) != 4) {
	      fprintf(stderr, "*** failed to parse '%s'\n", argv[i]);
	      usage(argv[0]);
	    }
			if (verbose) {
				log_add_message(LastEventTime, "Set monitor rect to %ix%i+%i+%i\n",
						InterestedDamageRect.width,InterestedDamageRect.height,
						InterestedDamageRect.x,InterestedDamageRect.y);
			}
			continue;
		}

		if (streq("-w", argv[i]) || streq("--wait", argv[i])) {
			if (++i>=argc) usage (argv[0]);
	  
			if (DamageWaitSecs >= 0) {
				fprintf(stderr, "Duplicate -w(--wait) option detected. Discarding the previous value\n");
			}
			if ((DamageWaitSecs = atoi(argv[i])) < 0) {
				fprintf(stderr, "*** failed to parse '%s'\n", argv[i]);
				usage(argv[0]);
			}
			if (verbose) log_add_message(LastEventTime, "Set event timeout to %isecs\n", DamageWaitSecs);

			continue;
		}
	
		if (streq("-b", argv[i]) || streq("--break", argv[i])) {
			if (BreakTimeout || BreakOnDamage) {
				fprintf(stderr, "Duplicate -b(--break)option detected. Discarding the previous value\n");
				BreakTimeout = 0;
				BreakOnDamage = 0;
			}
			if (++i>=argc) usage (argv[0]);
			
			if (!strncmp(argv[i], "damage", 6)) {
				sscanf(argv[i] + 6, ",%d", &BreakOnDamage);
				if (!BreakOnDamage) BreakOnDamage = 1;
				if (verbose) log_add_message(LastEventTime, "Break wait on the %d damage event\n", BreakOnDamage);
			}
			else {
				if ((BreakTimeout = atoi(argv[i])) < 0) {
					fprintf(stderr, "*** failed to parse '%s'\n", argv[i]);
					usage(argv[0]);
				}
				if (verbose) log_add_message(LastEventTime, "Set break timout to %imsecs\n", BreakTimeout);
			}
			continue;
		}

		if (streq("-d", argv[i]) || streq("--drag", argv[i])) {
      if (!XPointerDevice) {
        fprintf(stderr, "Failed to open pointer device, unable to simulate pointer events.\n");
        exit(-1);
      }
			if (inputEventsIndex == ASIZE(inputEvents)) {
				fprintf(stderr, "Too many input events specified\n");
				exit(-1);
			}
      if (!match_regex(argv[i + 1], "^([0-9]+,)?([0-9]+x[0-9]+,([0-9]+,)?)+[0-9]+x[0-9]+$")) {
        fprintf(stderr, "Failed to parse --drag options: %s\n", argv[i + 1]);
        exit(-1);
      }
			inputEvents[inputEventsIndex++] = i;
			
			if (++i>=argc) usage (argv[0]);
			continue;
		}

    if (streq("-k", argv[i]) || streq("--key", argv[i])) {
      if (!XKeyboardDevice) {
        fprintf(stderr, "Failed to open keyboard device, unable to simulate keyboard events.\n");
        exit(-1);
      }
			if (inputEventsIndex == ASIZE(inputEvents)) {
				fprintf(stderr, "Too many input events specified\n");
				exit(-1);
			}
			inputEvents[inputEventsIndex++] = i;
			if (++i>=argc) usage (argv[0]);
			
			continue;
		}
      
		if (streq("-t", argv[i]) || streq("--type", argv[i])) {
      if (!XKeyboardDevice) {
        fprintf(stderr, "Failed to open keyboard device, unable to simulate keyboard events.\n");
        exit(-1);
      }
			if (inputEventsIndex == ASIZE(inputEvents)) {
				fprintf(stderr, "Too many input events specified\n");
				exit(-1);
			}
			inputEvents[inputEventsIndex++] = i;
			if (++i>=argc) usage (argv[0]);
			
			if (!keysymMappingInitialized) {
				load_keycodes(dpy);
				keysymMappingInitialized = True;
			}
			
  		continue;
		}

		/* since moving from command sequence approach the inspect parameter is deprecated */
		if (streq("-i", argv[i]) || streq("--inspect", argv[i])) {
			if (verbose)
				log_add_message(LastEventTime, "Just displaying damage events until timeout\n");
			continue;
		}
	
		/* */
		if (streq("-u", argv[i]) || streq("--user", argv[i])) {
			start_user_input_monitoring(dpy);
			if (verbose)
				log_add_message(LastEventTime, "Reporting user input events\n");

			continue;
		}
		
		if (streq(argv[i], "-r") || streq(argv[i], "--response")) {
			if (++i>=argc) usage (argv[0]);
			char option[500];
			cnt = sscanf(argv[i], "%u,%s", &ResponseTimeout, option);
			if (cnt < 1) {
	      fprintf(stderr, "*** invalid response timeout value '%s'\n", argv[i]);
	      usage(argv[0]);
	    }
			if (cnt < 2) {
				SuppressReport = True;
			}
			else {
				if (strcmp(option, "verbose")) {
					fprintf(stderr, "*** invalid response option '%s'\n", argv[i]);
					usage(argv[0]);
				}
			}
			ScreenApplication = application_monitor(ROOT_WINDOW_RESOURCE);
			start_user_input_monitoring(dpy);
			if (verbose) log_add_message(LastEventTime, "Monitoring application response time\n");

			continue;
		}
		
		fprintf(stderr, "*** Dont understand  %s\n", argv[i]);
		usage(argv[0]);
	}

  /* start monitoring the root window if no targets are specified */
  if ((!WindowIndex && !ApplicationIndex) || ResponseTimeout) {
    application_monitor(ROOT_WINDOW_RESOURCE);
  }

	window_monitor_all(dpy);
	application_monitor_all(dpy);
  
	/* eat first damage event when BreakOnDamage set */
	if (BreakOnDamage) eat_damage(dpy);
	
	/* monitor the whole screen of no area is specified */
	if (!InterestedDamageRect.width && !InterestedDamageRect.height &&
				  !InterestedDamageRect.x && !InterestedDamageRect.y) {
		InterestedDamageRect.x = 0;
		InterestedDamageRect.y = 0;
		InterestedDamageRect.width  = DisplayWidth(dpy, DefaultScreen(dpy));
		InterestedDamageRect.height = DisplayHeight(dpy, DefaultScreen(dpy));
	}
	
	/* emulate user input */
	
	for (iEvent = 0; iEvent < inputEventsIndex; iEvent++) {
		i = inputEvents[iEvent];
		
		if (!strcmp("-c", argv[i]) || !strcmp("--click", argv[i])) {
			unsigned long delay = 0;
			Time start = 0;
			cnt = sscanf(argv[++i], "%ux%u,%lu", &x, &y, &delay);
			if (cnt == 2) {
        start = get_server_time(dpy);
				log_add_message(start, "Using no delay between press/release\n");
				delay = 0;
			}
			else if (cnt != 3) {
				fprintf(stderr, "cnt: %d\n", cnt);
				fprintf(stderr, "*** failed to parse '%s'\n", argv[i]);
				usage(argv[0]);
			}
			/* Send the event */
      process_events(dpy);
			start = fake_event(dpy, x, y, delay);
			log_add_message(start, "Clicked %ix%i\n", x, y);
      
			
			continue;
		}
		
		if (!strcmp("-d", argv[i]) || !strcmp("--drag", argv[i])) {
			Time drag_time;
			char *s = NULL, *p = NULL;
			unsigned long delay = 0;
			int first_drag = 1, button_state = XR_BUTTON_STATE_PRESS;
			
			s = p = argv[++i];
		  while (1) {
	      if (*p == ',' || *p == '\0') {
					Bool end = False;

					if (*p == '\0') {
						if (button_state == XR_BUTTON_STATE_PRESS) {
							fprintf(stderr, "*** Need at least 2 drag points!\n");
							usage(argv[0]);
						}
						
						/* last passed point so make sure button released */
						 button_state = XR_BUTTON_STATE_RELEASE;
						 end = True;
					}
					else *p = '\0';

					cnt = sscanf(s, "%ux%u", &x, &y);
					if (cnt == 2) {
						/* Send the event */
						drag_time = drag_event(dpy, x, y, button_state, delay);
            
						if (first_drag) {
							first_drag = 0;
						}
						log_add_message(drag_time, "Dragged to %ix%i\n", x, y);

						/* Make sure button state set to none after first point */
						button_state = XR_BUTTON_STATE_NONE;
					}
					else if (cnt == 1) {
						delay = x;
					}
					else {
						fprintf(stderr, "*** failed to parse '%s'\n", argv[i]);
						usage(argv[0]);
					}

					if (end) break;
					s = p+1;
				}
				p++;
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
	      log_add_message(LastEventTime, "Using default delay between press/release\n", delay);
	      delay = DefaultDelay;
			}
			else if (cnt != 3 || separator != ',') {
	      fprintf(stderr, "cnt: %d\n", cnt);
	      fprintf(stderr, "*** failed to parse '%s'\n", argv[i]);
	      if (key != NULL) free(key);
	      usage(argv[0]);
	    }
      process_events(dpy);
			start = send_key(dpy, key, delay);
			log_add_message(start, "Simulating keypress/-release pair (keycode '%s')\n",  key);
			free(key);
			
			continue;
		}
      
		if (!strcmp("-t", argv[i]) || !strcmp("--type", argv[i])) {
      if (!XKeyboardDevice) {
        fprintf(stderr, "Failed to open keyboard device, unable to simulate keyboard events.\n");
        exit(1);
      }
      
			Time start = send_string(dpy, argv[++i]);
			log_add_message(start, "Simulated keys for '%s'\n", argv[i]);
			
  		continue;
		}
		
	}

	/* setting the default wait period */
	if (DamageWaitSecs < 0) {
		DamageWaitSecs = 5;
	}
	
  signal(SIGINT, abort_wait);
	/* wait for damage events */
  rc = wait_response(dpy);

  scheduler_clear();

  log_write_messages();
  log_clear();


	if (EnableInputMonitoring == XT_TRUE) {
		XRecordDisableContext(dpy, RecordContext);
		XRecordFreeContext(dpy, RecordContext);
		XFlush(dpy);
		XCloseDisplay(RecordDisplay); 
	}
  if (XKeyboardDevice) XCloseDevice(dpy, XKeyboardDevice);
  if (XPointerDevice) XCloseDevice(dpy, XPointerDevice);
	
  /* Clean Up */
	window_remove_all(dpy);
	application_remove_all();
	
  XCloseDisplay(dpy);
  fclose(LogFile);
  
  return rc;
}

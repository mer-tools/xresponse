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
 * @file report.h
 * Reporting subsystem implementation.
 *
 * Because of asynchronous nature of XDamage and XRecord events it was
 * necessary to queue and sort them before printing, to ensure the events
 * are printed in correct order and the timestamp differences are correct.
 *
 * Response mode required custom output, which can be enabled by setting
 * raw mode with report_set_raw() function.
 */

#ifndef _REPORT_H_
#define _REPORT_H_

#include <stdbool.h>

/* Timestamp value to force message reuse the last message timestamp.
 * Usually used for informative messages.
 */
#define REPORT_LAST_TIMESTAMP	(-1)

/*
 * Defines the maximum time period in milliseconds for report queuing.
 */
#define REPORT_TIMEOUT          5000


/**
 * Initializes reporting subsystem.
 *
 * @param[in] filename   the report filename. Standard output is used
 *                       if the filename is NULL.
 */
void report_init(const char* filename);


/**
 * Releases resources allocated by report.
 */
void report_fini();


/**
 * Adds message to the report queue.
 *
 * This function allocates new report record, sets the specified timestamp,
 * formats the message and adds to the logger message queue.
 * @param[in] timestamp     the message timestamp. Use REPORT_LAST_TIMESTAMP to
 *                          use the timestamp of the last message.
 * @param[in] format        the message format (see printf specification).
 * @param ...               the message parameters.
 * @return
 */
void report_add_message(Time timestamp, const char* format, ...);


/**
 * Writes the report message queue to the defined output.
 *
 * The message queue is sorted before flushing.
 */
void report_flush_queue();


/**
 * Sets the raw operation mode.
 *
 * In raw operation mode the default output format is suppressed to allow custom
 * message formatting.
 * @param[in] value    true  - enable raw operation mode.
 *                     false - disable raw operation mode.
 */
void report_set_raw(bool value);


/**
 * Checks if raw reporting mode is enabled
 *
 * In raw mode no formatting is done to the output data while in normal
 * mode server timestamp and time difference values are printed before
 * output text.
 * @return
 */
bool report_get_raw();


#endif


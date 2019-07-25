/*
 * Copyright © 2018 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Christian J. Kellner <christian@kellner.me>
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

#define BOLT_MSEC_PER_SEC 1000LL   /* number of milli-seconds (ms) in a second (s) */
#define BOLT_USEC_PER_MSEC 1000LL  /* number of micro-seconds (µs) in a ms */

char *       bolt_epoch_format (guint64     seconds,
                                const char *format);

guint64      bolt_now_in_seconds (void);

G_END_DECLS

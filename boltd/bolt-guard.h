/*
 * Copyright © 2020 Red Hat, Inc
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

#include "bolt-enums.h"
#include "bolt-exported.h"

#include <sys/types.h>

G_BEGIN_DECLS

#define BOLT_TYPE_GUARD bolt_guard_get_type ()
G_DECLARE_FINAL_TYPE (BoltGuard, bolt_guard, BOLT, GUARD, GObject);

int                 bolt_guard_monitor (BoltGuard *guard,
                                        GError   **error);

const char *        bolt_guard_get_id (BoltGuard *guard);

const char *        bolt_guard_get_who (BoltGuard *guard);

guint               bolt_guard_get_pid (BoltGuard *guard);

const char *        bolt_guard_get_path (BoltGuard *guard);

const char *        bolt_guard_get_fifo (BoltGuard *guard);

GPtrArray *         bolt_guard_recover (const char *statedir,
                                        GError    **error);

gboolean            bolt_guard_save (BoltGuard *guard,
                                     GFile     *guarddir,
                                     GError   **error);

BoltGuard *         bolt_guard_load (const char *statedir,
                                     const char *name,
                                     GError    **error);

G_END_DECLS

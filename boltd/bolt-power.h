/*
 * Copyright © 2017 Red Hat, Inc
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
#include "bolt-guard.h"
#include "bolt-udev.h"

#include <sys/types.h>

G_BEGIN_DECLS

/* forward declaration */
struct udev;

/* BoltPower */

#define BOLT_TYPE_POWER bolt_power_get_type ()
G_DECLARE_FINAL_TYPE (BoltPower, bolt_power, BOLT, POWER, BoltExported);

BoltPower  *        bolt_power_new (BoltUdev *udev);

GFile *             bolt_power_get_statedir (BoltPower *power);

gboolean            bolt_power_can_force (BoltPower *power);

BoltPowerState      bolt_power_get_state (BoltPower *power);

BoltGuard *         bolt_power_acquire_full (BoltPower  *power,
                                             const char *who,
                                             pid_t       pid,
                                             GError    **error);

BoltGuard *         bolt_power_acquire (BoltPower *power,
                                        GError   **error);

GList *             bolt_power_list_guards (BoltPower *power);

G_END_DECLS

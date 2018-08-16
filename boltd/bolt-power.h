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
#include "bolt-udev.h"

G_BEGIN_DECLS

/* forward declaration */
struct udev;

/* BoltPowerGuard */

#define BOLT_TYPE_POWER_GUARD bolt_power_guard_get_type ()
G_DECLARE_FINAL_TYPE (BoltPowerGuard, bolt_power_guard, BOLT, POWER_GUARD, GObject);

/* BoltPower */

#define BOLT_TYPE_POWER bolt_power_get_type ()
G_DECLARE_FINAL_TYPE (BoltPower, bolt_power, BOLT, POWER, GObject);

BoltPower  *        bolt_power_new (BoltUdev *udev);

GFile *             bolt_power_get_statedir (BoltPower *power);

gboolean            bolt_power_can_force (BoltPower *power);

BoltPowerState      bolt_power_get_state (BoltPower *power);

BoltPowerGuard *    bolt_power_acquire_full (BoltPower  *power,
                                             const char *who,
                                             GError    **error);

BoltPowerGuard *    bolt_power_acquire (BoltPower *power,
                                        GError   **error);
G_END_DECLS

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

#include "bolt-enums.h"
#include "bolt-proxy.h"

G_BEGIN_DECLS

#define BOLT_TYPE_POWER bolt_power_get_type ()
G_DECLARE_FINAL_TYPE (BoltPower, bolt_power, BOLT, POWER, BoltProxy);


BoltPower *         bolt_power_new_for_object_path (GDBusConnection *bus,
                                                    GCancellable    *cancellable,
                                                    GError         **error);

/* methods */

int                 bolt_power_force_power (BoltPower *power,
                                            GError   **error);

GPtrArray *         bolt_power_list_guards (BoltPower    *power,
                                            GCancellable *cancellable,
                                            GError      **error);

/* getter */
gboolean            bolt_power_is_supported (BoltPower *power);

BoltPowerState      bolt_power_get_state (BoltPower *power);

/*  */

typedef struct BoltPowerGuard_
{
  char *id;
  char *who;
  guint pid;
} BoltPowerGuard;

void bolt_power_guard_free (BoltPowerGuard *guard);


G_END_DECLS

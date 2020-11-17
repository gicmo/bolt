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

#include <gio/gio.h>

G_BEGIN_DECLS

/* forward declaration */
struct udev;
struct udev_device;
struct udev_enumerate;

/* BoltUdev - small udev abstraction */
#define BOLT_TYPE_UDEV bolt_udev_get_type ()
G_DECLARE_FINAL_TYPE (BoltUdev, bolt_udev, BOLT, UDEV, GObject);

BoltUdev  *             bolt_udev_new (const char         *name,
                                       const char * const *filter,
                                       GError            **error);

struct udev_enumerate * bolt_udev_new_enumerate (BoltUdev *udev,
                                                 GError  **error);

struct udev_device * bolt_udev_device_new_from_syspath (BoltUdev   *udev,
                                                        const char *syspath,
                                                        GError    **error);

/* thunderbolt specific helpers */
int                  bolt_udev_count_hosts (BoltUdev *udev,
                                            GError  **error);

gboolean             bolt_udev_detect_force_power (BoltUdev *udev,
                                                   char    **path,
                                                   GError  **error);
G_END_DECLS

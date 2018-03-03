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

#include <glib.h>

struct udev;
struct udev_device;

G_BEGIN_DECLS

gboolean             bolt_sysfs_device_is_domain (struct udev_device *udev);
struct udev_device * bolt_sysfs_domain_for_device (struct udev_device *udev);
BoltSecurity         bolt_sysfs_security_for_device (struct udev_device *udev);

G_END_DECLS

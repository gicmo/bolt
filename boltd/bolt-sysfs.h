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

typedef enum BoltStatTime {
  BOLT_ST_ATIME,
  BOLT_ST_CTIME,
  BOLT_ST_MTIME
} BoltStatTime;

gint64               bolt_sysfs_device_get_time (struct udev_device *udev,
                                                 BoltStatTime        st);

gboolean             bolt_sysfs_device_is_domain (struct udev_device *udev,
                                                  GError            **error);

struct udev_device * bolt_sysfs_domain_for_device (struct udev_device *udev);

BoltSecurity         bolt_sysfs_security_for_device (struct udev_device *udev,
                                                     GError            **error);

int                  bolt_sysfs_count_domains (struct udev *udev,
                                               GError     **error);
typedef struct _BoltDevInfo
{

  /* always included  */
  gint   authorized;
  gssize keysize;
  gint   boot;

  /* if 'full' is true the rest is valid */
  gboolean    full;
  gint64      ctim;
  const char *syspath;
  const char *parent;       /* the uid */

} BoltDevInfo;

gboolean             bolt_sysfs_info_for_device (struct udev_device *udev,
                                                 gboolean            full,
                                                 BoltDevInfo        *info,
                                                 GError            **error);

G_END_DECLS

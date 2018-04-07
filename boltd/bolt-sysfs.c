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

#include "config.h"

#include "bolt-sysfs.h"

#include "bolt-error.h"
#include "bolt-str.h"
#include "bolt-log.h"

#include <libudev.h>
#include <sys/stat.h>

gint64
bolt_sysfs_device_get_time (struct udev_device *udev,
                            BoltStatTime        st)
{
  const char *path;
  struct stat sb;
  gint64 ms = 0;
  int r;

  path = udev_device_get_syspath (udev);

  if (path == NULL)
    return 0;

  r = lstat (path, &sb);

  if (r == -1)
    return 0;

  switch (st)
    {
    case BOLT_ST_CTIME:
      ms = (gint64) sb.st_ctim.tv_sec;
      break;

    case BOLT_ST_ATIME:
      ms = (gint64) sb.st_atim.tv_sec;
      break;

    case BOLT_ST_MTIME:
      ms = (gint64) sb.st_mtim.tv_sec;
      break;
    }

  if (ms < 0)
    ms = 0;

  return ms;
}

gboolean
bolt_sysfs_device_is_domain (struct udev_device *udev)
{
  const char *devtype = udev_device_get_devtype (udev);

  return bolt_streq (devtype, "thunderbolt_domain");
}

struct udev_device *
bolt_sysfs_domain_for_device (struct udev_device *udev)
{
  struct udev_device *parent;
  gboolean found;

  found = FALSE;
  parent = udev;
  do
    {
      parent = udev_device_get_parent (parent);
      if (!parent)
        break;

      found = bolt_sysfs_device_is_domain (parent);
    }
  while (!found);

  return found ? parent : NULL;
}

BoltSecurity
bolt_sysfs_security_for_device (struct udev_device *udev,
                                GError            **error)
{
  struct udev_device *parent = NULL;
  const char *v;
  BoltSecurity s;

  if (bolt_sysfs_device_is_domain (udev))
    parent = udev;
  else
    parent = bolt_sysfs_domain_for_device (udev);

  if (parent == NULL)
    {
      g_set_error_literal (error, BOLT_ERROR, BOLT_ERROR_UDEV,
                           "failed to determine domain device");
      return BOLT_SECURITY_UNKNOWN;
    }

  v = udev_device_get_sysattr_value (parent, "security");
  s = bolt_security_from_string (v);

  if (!bolt_security_validate (s))
    {
      g_set_error (error, BOLT_ERROR, BOLT_ERROR_UDEV,
                   "unknown security level '%s'", v);
      s = BOLT_SECURITY_UNKNOWN;
    }

  return s;
}

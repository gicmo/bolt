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

#include <errno.h>
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
bolt_sysfs_device_is_domain (struct udev_device *udev,
                             GError            **error)
{
  const char *devtype = udev_device_get_devtype (udev);
  const char *subsystem = udev_device_get_subsystem (udev);
  gboolean is_domain;

  is_domain = bolt_streq (subsystem, "thunderbolt") &&
              bolt_streq (devtype, "thunderbolt_domain");

  if (!is_domain)
    {
      const char *syspath = udev_device_get_syspath (udev);
      g_set_error (error, BOLT_ERROR, BOLT_ERROR_UDEV,
                   "device '%s' is not a thunderbolt domain",
                   syspath);
    }

  return is_domain;
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

      found = bolt_sysfs_device_is_domain (parent, NULL);
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

  if (bolt_sysfs_device_is_domain (udev, NULL))
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
  s = bolt_enum_from_string (BOLT_TYPE_SECURITY, v, error);

  return s;
}


int
bolt_sysfs_count_domains (struct udev *udev,
                          GError     **error)
{
  struct udev_enumerate *e;
  struct udev_list_entry *l, *devices;
  int r, count = 0;

  e = udev_enumerate_new (udev);

  udev_enumerate_add_match_subsystem (e, "thunderbolt");
  udev_enumerate_add_match_property (e, "DEVTYPE", "thunderbolt_domain");

  r = udev_enumerate_scan_devices (e);
  if (r < 0)
    {
      g_set_error (error, BOLT_ERROR, BOLT_ERROR_UDEV,
                   "failed to scan udev: %s",
                   g_strerror (-r));
      return r;
    }

  devices = udev_enumerate_get_list_entry (e);
  udev_list_entry_foreach (l, devices)
    count++;

  udev_enumerate_unref (e);

  return count;
}

static gint
sysfs_get_sysattr_value_as_int (struct udev_device *udev,
                                const char         *attr)
{
  const char *str;
  gboolean ok;
  gint val;

  g_return_val_if_fail (udev != NULL, FALSE);

  str = udev_device_get_sysattr_value (udev, attr);
  if (str == NULL)
    return -errno;

  ok = bolt_str_parse_as_int (str, &val);

  if (!ok)
    return -errno;

  return val;
}

static gssize
sysfs_get_sysattr_size (struct udev_device *udev,
                        const char         *attr)
{
  const char *str;

  g_return_val_if_fail (udev != NULL, FALSE);

  str = udev_device_get_sysattr_value (udev, attr);
  if (str == NULL)
    return -errno;

  return strlen (str);
}

gboolean
bolt_sysfs_info_for_device (struct udev_device *udev,
                            gboolean            full,
                            BoltDevInfo        *info,
                            GError            **error)
{
  struct udev_device *parent;
  int auth;

  g_return_val_if_fail (udev != NULL, FALSE);
  g_return_val_if_fail (info != NULL, FALSE);

  info->keysize = -1;
  info->ctim = -1;
  info->full = FALSE;
  info->parent = NULL;

  auth = sysfs_get_sysattr_value_as_int (udev, "authorized");
  info->authorized = auth;

  if (auth < 0)
    {
      int code = g_io_error_from_errno (errno);
      g_set_error (error, G_IO_ERROR, code,
                   "could not read 'authorized': %s",
                   g_strerror (errno));
      return FALSE;
    }

  info->keysize = sysfs_get_sysattr_size (udev, "key");
  info->boot = sysfs_get_sysattr_value_as_int (udev, "boot");

  if (full == FALSE)
    return TRUE;

  info->full = TRUE;
  info->ctim = bolt_sysfs_device_get_time (udev, BOLT_ST_CTIME);
  info->syspath = udev_device_get_syspath (udev);

  parent = udev_device_get_parent (udev);

  if (parent != NULL)
    info->parent = udev_device_get_sysattr_value (parent, "unique_id");

  return TRUE;
}

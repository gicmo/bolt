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

#include "bolt-str.h"
#include "bolt-log.h"

#include <libudev.h>

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
bolt_sysfs_security_for_device (struct udev_device *udev)
{
  struct udev_device *parent = NULL;
  const char *v;
  BoltSecurity s;

  parent = bolt_sysfs_domain_for_device (udev);
  if (parent == NULL)
    {
      bolt_warn ("failed to determine domain device");
      return BOLT_SECURITY_NONE;
    }

  v = udev_device_get_sysattr_value (parent, "security");
  s = bolt_security_from_string (v);

  if (!bolt_security_validate (s))
    {
      bolt_warn ("invalid security: %s", v);
      s = BOLT_SECURITY_NONE;
    }

  return s;
}

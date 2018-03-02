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

#include "bolt-power.h"

#include "bolt-log.h"
#include "bolt-io.h"
#include "bolt-str.h"

#include <libudev.h>

#define INTEL_WMI_THUNDERBOLT_GUID "86CCFD48-205E-4A77-9C48-2021CBEDE341"

typedef struct udev_device udev_device;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (udev_device, udev_device_unref);

struct _BoltPower
{
  GObject object;

  /* the actual key plus the null char */
  char *path;

};

enum {
  PROP_0,

  PROP_SUPPORTED,

  PROP_LAST
};

static GParamSpec *power_props[PROP_LAST] = { NULL, };

G_DEFINE_TYPE (BoltPower,
               bolt_power,
               G_TYPE_OBJECT);


static void
bolt_power_finalize (GObject *object)
{
  BoltPower *power = BOLT_POWER (object);

  g_clear_pointer (&power->path, g_free);

  G_OBJECT_CLASS (bolt_power_parent_class)->finalize (object);
}


static void
bolt_power_init (BoltPower *power)
{
}

static void
bolt_power_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  BoltPower *power = BOLT_POWER (object);

  switch (prop_id)
    {
    case PROP_SUPPORTED:
      g_value_set_boolean (value, power->path != NULL);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bolt_power_class_init (BoltPowerClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = bolt_power_finalize;

  gobject_class->get_property = bolt_power_get_property;

  power_props[PROP_SUPPORTED] =
    g_param_spec_boolean ("supported",
                          NULL, NULL,
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_NICK);

  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     power_props);
}

BoltPower *
bolt_power_new (struct udev *udev)
{
  struct udev_enumerate *e;
  struct udev_list_entry *l, *devices;
  BoltPower *power;

  power = g_object_new (BOLT_TYPE_POWER, NULL);

  e = udev_enumerate_new (udev);
  udev_enumerate_add_match_subsystem (e, "wmi");
  udev_enumerate_add_match_property (e, "DRIVER", "intel-wmi-thunderbolt");

  udev_enumerate_scan_devices (e);
  devices = udev_enumerate_get_list_entry (e);

  udev_list_entry_foreach (l, devices)
    {
      g_autofree char *path = NULL;
      const char *syspath;

      syspath = udev_list_entry_get_name (l);
      path = g_build_filename (syspath, "force_power", NULL);

      if (g_file_test (path, G_FILE_TEST_IS_REGULAR))
        {
          power->path = g_steal_pointer (&path);
          break;
        }
    }

  udev_enumerate_unref (e);
  return power;
}

gboolean
bolt_power_can_force (BoltPower *power)
{
  return power->path != NULL;
}

gboolean
bolt_power_force_switch (BoltPower *power,
                         gboolean   on,
                         GError   **error)
{
  gboolean ok;
  int fd;

  g_return_val_if_fail (BOLT_IS_POWER (power), FALSE);

  if (power->path == NULL)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                           "force power not supported");
      return FALSE;
    }

  fd = bolt_open (power->path, O_WRONLY, 0, error);
  if (fd < 0)
    return FALSE;

  ok = bolt_write_all (fd, on ? "1" : "0", 1, error);
  bolt_close (fd, NULL);

  return ok;
}

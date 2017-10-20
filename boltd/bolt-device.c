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

#include "bolt-device.h"
#include "bolt-error.h"

#include <libudev.h>

struct _BoltDevice
{
  BoltDBusDeviceSkeleton object;

  char                  *uid;
};


enum {
  PROP_0,

  PROP_UID,

  PROP_LAST
};


G_DEFINE_TYPE (BoltDevice,
               bolt_device,
               BOLT_DBUS_TYPE_DEVICE_SKELETON)

static void
bolt_device_finalize (GObject *object)
{
  BoltDevice *dev = BOLT_DEVICE (object);

  g_free (dev->uid);

  G_OBJECT_CLASS (bolt_device_parent_class)->finalize (object);
}

static void
bolt_device_init (BoltDevice *mgr)
{

}

static void
bolt_device_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  BoltDevice *dev = BOLT_DEVICE (object);

  switch (prop_id)
    {
    case PROP_UID:
      g_value_set_string (value, dev->uid);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bolt_device_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  BoltDevice *dev = BOLT_DEVICE (object);

  switch (prop_id)
    {
    case PROP_UID:
      dev->uid = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}


static void
bolt_device_class_init (BoltDeviceClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = bolt_device_finalize;

  gobject_class->get_property = bolt_device_get_property;
  gobject_class->set_property = bolt_device_set_property;

  g_object_class_override_property (gobject_class,
                                    PROP_UID,
                                    "uid");
}

/* public methods */

BoltDevice *
bolt_device_new_for_udev (BoltManager        *mgr,
                          struct udev_device *udev,
                          GError            **error)
{
  BoltDevice *dev;
  const char *uid;

  uid = udev_device_get_sysattr_value (udev, "unique_id");
  if (uid == NULL)
    {
      g_set_error_literal (error,
                           BOLT_ERROR, BOLT_ERROR_UDEV,
                           "failed to read unique_id");
      return NULL;
    }

  dev = g_object_new (BOLT_TYPE_DEVICE,
                      "uid", uid,
                      NULL);

  return dev;
}

gboolean
bolt_device_export (BoltDevice      *device,
                    GDBusConnection *connection,
                    GError         **error)
{
  g_autofree char *path = NULL;

  path = g_strdup_printf ("/org/freedesktop/Bolt/devices/%s", device->uid);
  g_strdelimit (path, "-", '_');

  g_debug ("Exporting device at: %s", path);

  return g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (device),
                                           connection,
                                           path,
                                           error);
}

void
bolt_device_unexport (BoltDevice *device)
{
  g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (device));
}

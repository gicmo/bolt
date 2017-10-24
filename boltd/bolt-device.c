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

#include "bolt-dbus.h"
#include "bolt-device.h"
#include "bolt-error.h"
#include "bolt-io.h"
#include "bolt-manager.h"

#include <dirent.h>
#include <libudev.h>

struct _BoltDevice
{
  BoltDBusDeviceSkeleton object;

  /* weak reference */
  BoltManager *mgr;

  char        *dbus_path;

  char        *uid;
  char        *name;
  char        *vendor;

  BoltStatus   status;

  /* when device is attached */
  char *syspath;
  DIR  *devdir;
};


enum {
  PROP_0,

  PROP_UID,
  PROP_NAME,
  PROP_VENDOR,
  PROP_STATUS,
  PROP_SYSFS,

  PROP_LAST
};


G_DEFINE_TYPE (BoltDevice,
               bolt_device,
               BOLT_DBUS_TYPE_DEVICE_SKELETON)

static void
bolt_device_finalize (GObject *object)
{
  BoltDevice *dev = BOLT_DEVICE (object);

  g_free (dev->dbus_path);

  g_free (dev->uid);
  g_free (dev->name);
  g_free (dev->vendor);
  g_free (dev->syspath);

  if (dev->devdir)
    closedir (dev->devdir);

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

    case PROP_NAME:
      g_value_set_string (value, dev->name);
      break;

    case PROP_VENDOR:
      g_value_set_string (value, dev->vendor);
      break;

    case PROP_STATUS:
      g_value_set_uint (value, dev->status);
      break;

    case PROP_SYSFS:
      g_value_set_string (value, dev->syspath);
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

    case PROP_NAME:
      dev->vendor = g_value_dup_string (value);
      break;

    case PROP_VENDOR:
      dev->name = g_value_dup_string (value);
      break;

    case PROP_STATUS:
      dev->status = g_value_get_uint (value);
      break;

    case PROP_SYSFS:
      dev->syspath = g_value_dup_string (value);
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

  g_object_class_override_property (gobject_class,
                                    PROP_NAME,
                                    "name");

  g_object_class_override_property (gobject_class,
                                    PROP_VENDOR,
                                    "vendor");

  g_object_class_override_property (gobject_class,
                                    PROP_STATUS,
                                    "status");

  g_object_class_override_property (gobject_class,
                                    PROP_SYSFS,
                                    "sysfs-path");

}

/* internal methods */

static char *
read_sysattr_name (int fd, const char *attr, GError **error)
{
  g_autofree char *s = NULL;
  char *v;

  s = g_strdup_printf ("%s_name", attr);

  v = bolt_read_value_at (fd, s, NULL);
  if (!v)
    v = bolt_read_value_at (fd, attr, error);

  return v;
}

static gint
read_sysfs_attr_int (struct udev_device *device, const char *attr)
{
  const char *str;
  char *end;
  gint64 val;

  str = udev_device_get_sysattr_value (device, attr);

  if (str == NULL)
    return 0;

  val = g_ascii_strtoull (str, &end, 0);

  if (str == end)
    return 0;

  if (val > G_MAXINT || val < G_MININT)
    {
      g_warning ("value read from sysfs outside of guint's range.");
      val = 0;
    }

  return (gint) val;
}

static gboolean
string_nonzero (const char *str)
{
  return str != NULL && str[0] != '\0';
}

static BoltStatus
bolt_status_from_udev (struct udev_device *udev)
{
  gint authorized;
  const char *key;
  gboolean have_key;

  authorized = read_sysfs_attr_int (udev, "authorized");

  if (authorized == 2)
    return BOLT_STATUS_AUTHORIZED_SECURE;

  key = udev_device_get_sysattr_value (udev, "key");
  have_key = string_nonzero (key);

  if (authorized == 1)
    {
      if (have_key)
        return BOLT_STATUS_AUTHORIZED_NEWKEY;
      else
        return BOLT_STATUS_AUTHORIZED;
    }
  else if (authorized == 0 && have_key)
    {
      return BOLT_STATUS_AUTH_ERROR;
    }

  return BOLT_STATUS_CONNECTED;
}

/* public methods */

BoltDevice *
bolt_device_new_for_udev (BoltManager        *mgr,
                          struct udev_device *udev,
                          GError            **error)
{
  g_autofree char *uid = NULL;
  g_autofree char *name = NULL;
  g_autofree char *vendor = NULL;

  g_autoptr(DIR) devdir = NULL;
  BoltDevice *dev;
  const char *sysfs;
  int fd;

  sysfs = udev_device_get_syspath (udev);
  devdir = bolt_opendir (sysfs, error);

  if (devdir == NULL)
    return NULL;

  fd = dirfd (devdir);

  uid = bolt_read_value_at (fd, "unique_id", error);
  if (uid == NULL)
    return NULL;

  name = read_sysattr_name (fd, "device", error);
  if (name == NULL)
    return NULL;

  vendor = read_sysattr_name (fd, "vendor", error);
  if (vendor == NULL)
    return NULL;

  dev = g_object_new (BOLT_TYPE_DEVICE,
                      NULL);

  dev->uid = g_steal_pointer (&uid);
  dev->name = g_steal_pointer (&name);
  dev->vendor = g_steal_pointer (&vendor);
  dev->syspath = g_strdup (sysfs);
  dev->devdir = g_steal_pointer (&devdir);
  dev->status = bolt_status_from_udev (udev);

  g_object_add_weak_pointer (G_OBJECT (mgr),
                             (gpointer *) &dev->mgr);

  return dev;
}

const char *
bolt_device_export (BoltDevice      *device,
                    GDBusConnection *connection,
                    GError         **error)
{
  const char *path;
  gboolean ok;

  g_return_val_if_fail (BOLT_IS_DEVICE (device), FALSE);

  path = bolt_device_get_object_path (device);

  g_debug ("Exporting device at: %s", path);

  ok = g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (device),
                                         connection,
                                         path,
                                         error);
  return ok ? path : NULL;
}

void
bolt_device_unexport (BoltDevice *device)
{
  const char *path;

  path = bolt_device_get_object_path (device);

  g_debug ("Unexporting device at: %s", path);
  g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (device));
}

const char *
bolt_device_get_object_path (BoltDevice *device)
{
  g_return_val_if_fail (BOLT_IS_DEVICE (device), FALSE);

  if (device->dbus_path == NULL)
    {
      char *path = NULL;

      path = g_strdup_printf ("/org/freedesktop/Bolt/devices/%s", device->uid);
      g_strdelimit (path, "-", '_');

      device->dbus_path = path;
    }

  return device->dbus_path;
}

const char *
bolt_device_get_uid (BoltDevice *dev)
{
  return dev->uid;
}

const char *
bolt_device_get_syspath (BoltDevice *dev)
{
  return dev->syspath;
}

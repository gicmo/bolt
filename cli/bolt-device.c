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
#include "bolt-enums.h"
#include "bolt-error.h"

#include <gio/gio.h>

struct _BoltDevice
{
  BoltProxy parent;
};

enum {
  PROP_0,

  /* D-Bus Props */
  PROP_UID,
  PROP_NAME,
  PROP_VENDOR,

  PROP_LAST
};

static GParamSpec *props[PROP_LAST] = {NULL, };

G_DEFINE_TYPE (BoltDevice,
               bolt_device,
               BOLT_TYPE_PROXY);

static void
bolt_device_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  if (bolt_proxy_get_dbus_property (object, prop_id, value))
    return;
}

static const BoltProxyProp *
bolt_device_get_dbus_props (guint *n)
{
  static BoltProxyProp dbus_props[] = {
    {"Uid",      "uid",       PROP_UID,        NULL},
    {"Name",     "name",      PROP_NAME,       NULL},
    {"Vendor",   "vendor",    PROP_VENDOR,     NULL},
  };

  *n = G_N_ELEMENTS (dbus_props);

  return dbus_props;
}


static void
bolt_device_class_init (BoltDeviceClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  BoltProxyClass *proxy_class = BOLT_PROXY_CLASS (klass);

  gobject_class->get_property = bolt_device_get_property;

  proxy_class->get_dbus_props = bolt_device_get_dbus_props;

  props[PROP_UID] =
    g_param_spec_string ("uid",
                         NULL, NULL,
                         "unknown",
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_NICK);

  props[PROP_NAME] =
    g_param_spec_string ("name",
                         NULL, NULL,
                         "unknown",
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_NICK);

  props[PROP_VENDOR] =
    g_param_spec_string ("vendor",
                         NULL, NULL,
                         "unknown",
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_NICK);


  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     props);

}

static void
bolt_device_init (BoltDevice *mgr)
{
}

/* public methods */

BoltDevice *
bolt_device_new_for_object_path (GDBusConnection *bus,
                                 const char      *path,
                                 GError         **error)
{
  BoltDevice *dev;

  dev = g_initable_new (BOLT_TYPE_DEVICE,
                        NULL, error,
                        "bus", bus,
                        "object-path", path,
                        "interface-name", BOLT_DBUS_DEVICE_INTERFACE,
                        NULL);

  return dev;
}

gboolean
bolt_device_authorize (BoltDevice *dev,
                       GError    **error)
{
  g_autoptr(GError) err = NULL;
  GDBusProxy *proxy;

  g_return_val_if_fail (BOLT_IS_DEVICE (dev), FALSE);

  proxy = bolt_proxy_get_proxy (BOLT_PROXY (dev));
  g_dbus_proxy_call_sync (proxy,
                          "Authorize",
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          NULL,
                          &err);

  if (err != NULL)
    {
      if (g_dbus_error_is_remote_error (err))
        g_dbus_error_strip_remote_error (err);

      g_propagate_error (error, g_steal_pointer (&err));
      return FALSE;
    }

  return TRUE;
}

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

#include "bolt-enums.h"
#include "bolt-error.h"
#include "bolt-names.h"

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
  PROP_TYPE,
  PROP_STATUS,
  PROP_PARENT,
  PROP_SYSPATH,
  PROP_SECURITY,
  PROP_CONNTIME,
  PROP_AUTHTIME,

  PROP_STORED,
  PROP_POLICY,
  PROP_KEY,
  PROP_STORETIME,
  PROP_LABEL,

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
    {"Uid",           "uid",        PROP_UID,           NULL},
    {"Name",          "name",       PROP_NAME,          NULL},
    {"Vendor",        "vendor",     PROP_VENDOR,        NULL},
    {"Type",          "type",       PROP_TYPE,          NULL},
    {"Status",        "status",     PROP_STATUS,        NULL},
    {"Parent",        "parent",     PROP_PARENT,        NULL},
    {"SysfsPath",     "syspath",    PROP_SYSPATH,       NULL},
    {"Security",      "security",   PROP_SECURITY,      NULL},
    {"ConnectTime",   "conntime",   PROP_CONNTIME,      NULL},
    {"AuthorizeTime", "authtime",   PROP_AUTHTIME,      NULL},
    {"Stored",        "stored",     PROP_STORED,        NULL},
    {"Policy",        "policy",     PROP_POLICY,        NULL},
    {"Key",           "key",        PROP_KEY,           NULL},
    {"StoreTime",    "storetime",  PROP_STORETIME,     NULL},
    {"Label",         "label",      PROP_LABEL,         NULL}
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

  props[PROP_TYPE] =
    g_param_spec_enum ("type",
                       NULL, NULL,
                       BOLT_TYPE_DEVICE_TYPE,
                       BOLT_DEVICE_PERIPHERAL,
                       G_PARAM_READABLE |
                       G_PARAM_STATIC_NICK);

  props[PROP_STATUS] =
    g_param_spec_enum ("status",
                       NULL, NULL,
                       BOLT_TYPE_STATUS,
                       BOLT_STATUS_DISCONNECTED,
                       G_PARAM_READABLE |
                       G_PARAM_STATIC_NICK);

  props[PROP_PARENT] =
    g_param_spec_string ("parent",
                         NULL, NULL,
                         "unknown",
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_NICK);

  props[PROP_SYSPATH] =
    g_param_spec_string ("syspath",
                         NULL, NULL,
                         "unknown",
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_NICK);

  props[PROP_SECURITY] =
    g_param_spec_enum ("security",
                       NULL, NULL,
                       BOLT_TYPE_SECURITY,
                       BOLT_SECURITY_NONE,
                       G_PARAM_READABLE |
                       G_PARAM_STATIC_NICK);

  props[PROP_CONNTIME] =
    g_param_spec_uint64 ("conntime",
                         "ConnectTime", NULL,
                         0, G_MAXUINT64, 0,
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_AUTHTIME] =
    g_param_spec_uint64 ("authtime",
                         "AuthorizeTime", NULL,
                         0, G_MAXUINT64, 0,
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_STORED] =
    g_param_spec_boolean ("stored",
                          NULL, NULL,
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_NICK);

  props[PROP_POLICY] =
    g_param_spec_enum ("policy",
                       NULL, NULL,
                       BOLT_TYPE_POLICY,
                       BOLT_POLICY_DEFAULT,
                       G_PARAM_READABLE |
                       G_PARAM_STATIC_NICK);

  props[PROP_KEY] =
    g_param_spec_enum ("key",
                       NULL, NULL,
                       BOLT_TYPE_KEY_STATE,
                       BOLT_KEY_MISSING,
                       G_PARAM_READABLE |
                       G_PARAM_STATIC_NICK);

  props[PROP_STORETIME] =
    g_param_spec_uint64 ("storetime",
                         "StoreTime", NULL,
                         0, G_MAXUINT64, 0,
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_LABEL] =
    g_param_spec_string ("label",
                         "Label", NULL,
                         NULL,
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_STRINGS);

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
                        "g-flags", G_DBUS_PROXY_FLAGS_NONE,
                        "g-connection", bus,
                        "g-name", BOLT_DBUS_NAME,
                        "g-object-path", path,
                        "g-interface-name", BOLT_DBUS_DEVICE_INTERFACE,
                        NULL);

  return dev;
}

gboolean
bolt_device_authorize (BoltDevice   *dev,
                       BoltAuthFlags flags,
                       GError      **error)
{
  g_autoptr(GError) err = NULL;

  g_return_val_if_fail (BOLT_IS_DEVICE (dev), FALSE);

  g_dbus_proxy_call_sync (G_DBUS_PROXY (dev),
                          "Authorize",
                          g_variant_new ("(u)", (guint32) flags),
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

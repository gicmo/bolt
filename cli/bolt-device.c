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
#include "bolt-wire.h"

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
  PROP_GEN,
  PROP_TYPE,
  PROP_STATUS,
  PROP_AUTHFLAGS,
  PROP_PARENT,
  PROP_SYSPATH,
  PROP_DOMAIN,
  PROP_CONNTIME,
  PROP_AUTHTIME,
  PROP_LINKSPEED,

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
bolt_device_class_init (BoltDeviceClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = bolt_proxy_property_getter;
  gobject_class->set_property = bolt_proxy_property_setter;

  props[PROP_UID] =
    g_param_spec_string ("uid", "Uid",
                         "The unique identifier.",
                         "unknown",
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_NAME] =
    g_param_spec_string ("name", "Name",
                         "Human readable device name.",
                         "unknown",
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_VENDOR] =
    g_param_spec_string ("vendor", "Vendor",
                         "The name of the device vendor",
                         "unknown",
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_STRINGS);
  props[PROP_GEN] =
    g_param_spec_uint ("generation", "Generation",
                       "The generation of the controller chip.",
                       0, G_MAXUINT, 0,
                       G_PARAM_READABLE |
                       G_PARAM_STATIC_STRINGS);

  props[PROP_TYPE] =
    g_param_spec_enum ("type", "Type",
                       "The type, i.e. host or peripheral.",
                       BOLT_TYPE_DEVICE_TYPE,
                       BOLT_DEVICE_PERIPHERAL,
                       G_PARAM_READABLE |
                       G_PARAM_STATIC_STRINGS);

  props[PROP_STATUS] =
    g_param_spec_enum ("status", "Status",
                       "The device status.",
                       BOLT_TYPE_STATUS,
                       BOLT_STATUS_DISCONNECTED,
                       G_PARAM_READABLE |
                       G_PARAM_STATIC_STRINGS);

  props[PROP_AUTHFLAGS] =
    g_param_spec_flags ("authflags", "AuthFlags",
                        "Flags describing the authentication state.",
                        BOLT_TYPE_AUTH_FLAGS,
                        BOLT_AUTH_NONE,
                        G_PARAM_READABLE |
                        G_PARAM_STATIC_STRINGS);

  props[PROP_PARENT] =
    g_param_spec_string ("parent", "Parent",
                         "Unique identifier of the parent.",
                         "unknown",
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_SYSPATH] =
    g_param_spec_string ("syspath", "SysfsPath",
                         "The sysfs path of the udev device.",
                         "unknown",
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_DOMAIN] =
    g_param_spec_string ("domain", "Domain",
                         "Unique id of the corresponding domain.",
                         "unknown",
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_STRINGS);


  props[PROP_CONNTIME] =
    g_param_spec_uint64 ("conntime", "ConnectTime",
                         "When was the device connected?",
                         0, G_MAXUINT64, 0,
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_AUTHTIME] =
    g_param_spec_uint64 ("authtime", "AuthorizeTime",
                         "When was the device authorized?",
                         0, G_MAXUINT64, 0,
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_LINKSPEED] =
    g_param_spec_boxed ("linkspeed", "LinkSpeed",
                        "The speed to the parent",
                        BOLT_TYPE_LINK_SPEED,
                        G_PARAM_READABLE |
                        G_PARAM_STATIC_STRINGS);

  props[PROP_STORED] =
    g_param_spec_boolean ("stored", "Stored",
                          "Is the device recorded in the database?",
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS);

  props[PROP_POLICY] =
    g_param_spec_enum ("policy", "Policy",
                       "What to do when the device is connected?",
                       BOLT_TYPE_POLICY,
                       BOLT_POLICY_DEFAULT,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS);

  props[PROP_KEY] =
    g_param_spec_enum ("key", "Key",
                       "State of the device key.",
                       BOLT_TYPE_KEY_STATE,
                       BOLT_KEY_MISSING,
                       G_PARAM_READABLE |
                       G_PARAM_STATIC_STRINGS);

  props[PROP_STORETIME] =
    g_param_spec_uint64 ("storetime", "StoreTime",
                         "When was the device stored?",
                         0, G_MAXUINT64, 0,
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_LABEL] =
    g_param_spec_string ("label", "Label",
                         "The name given by bolt or the user.",
                         NULL,
                         G_PARAM_READWRITE |
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
                                 GCancellable    *cancel,
                                 GError         **error)
{
  g_autoptr(BoltDevice) dev = NULL;
  gboolean ok;

  g_return_val_if_fail (G_IS_DBUS_CONNECTION (bus), NULL);
  g_return_val_if_fail (path != NULL, NULL);
  g_return_val_if_fail (!cancel || G_IS_CANCELLABLE (cancel), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  dev = g_initable_new (BOLT_TYPE_DEVICE,
                        cancel, error,
                        "g-flags", G_DBUS_PROXY_FLAGS_NONE,
                        "g-connection", bus,
                        "g-name", BOLT_DBUS_NAME,
                        "g-object-path", path,
                        "g-interface-name", BOLT_DBUS_DEVICE_INTERFACE,
                        NULL);

  if (dev == NULL)
    return NULL;

  ok = bolt_proxy_set_wireconv (BOLT_PROXY (dev),
                                props[PROP_LINKSPEED],
                                "linkspeed",
                                bolt_link_speed_to_wire,
                                bolt_link_speed_from_wire,
                                error);

  if (!ok)
    return NULL;

  return g_steal_pointer (&dev);
}

gboolean
bolt_device_authorize (BoltDevice   *dev,
                       BoltAuthCtrl  flags,
                       GCancellable *cancel,
                       GError      **error)
{
  g_autoptr(GError) err = NULL;
  g_autofree char *fstr = NULL;

  g_return_val_if_fail (BOLT_IS_DEVICE (dev), FALSE);
  g_return_val_if_fail (!cancel || G_IS_CANCELLABLE (cancel), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  fstr = bolt_flags_to_string (BOLT_TYPE_AUTH_CTRL, flags, error);
  if (fstr == NULL)
    return FALSE;

  g_dbus_proxy_call_sync (G_DBUS_PROXY (dev),
                          "Authorize",
                          g_variant_new ("(s)", fstr),
                          G_DBUS_CALL_FLAGS_NONE,
                          -1,
                          cancel,
                          &err);

  if (err != NULL)
    return bolt_error_propagate_stripped (error, &err);

  return TRUE;
}

void
bolt_device_authorize_async (BoltDevice         *dev,
                             BoltAuthCtrl        flags,
                             GCancellable       *cancellable,
                             GAsyncReadyCallback callback,
                             gpointer            user_data)
{
  GError *err = NULL;
  g_autofree char *fstr = NULL;

  g_return_if_fail (BOLT_IS_DEVICE (dev));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));
  g_return_if_fail (callback != NULL);

  fstr = bolt_flags_to_string (BOLT_TYPE_AUTH_CTRL, flags, &err);
  if (fstr == NULL)
    {
      g_task_report_error (dev, callback, user_data, NULL, err);
      return;
    }

  g_dbus_proxy_call (G_DBUS_PROXY (dev),
                     "Authorize",
                     g_variant_new ("(s)", fstr),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     cancellable,
                     callback,
                     user_data);
}

gboolean
bolt_device_authorize_finish (BoltDevice   *dev,
                              GAsyncResult *res,
                              GError      **error)
{
  g_autoptr(GError) err = NULL;
  g_autoptr(GVariant) val = NULL;

  g_return_val_if_fail (BOLT_IS_DEVICE (dev), FALSE);
  g_return_val_if_fail (G_IS_ASYNC_RESULT (res), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  val = g_dbus_proxy_call_finish (G_DBUS_PROXY (dev), res, &err);
  if (val == NULL)
    {
      bolt_error_propagate_stripped (error, &err);
      return FALSE;
    }

  return TRUE;
}

const char *
bolt_device_get_uid (BoltDevice *dev)
{
  const char *str;

  g_return_val_if_fail (BOLT_IS_DEVICE (dev), NULL);

  str = bolt_proxy_get_string_by_pspec (dev, props[PROP_UID]);

  return str;
}

const char *
bolt_device_get_name (BoltDevice *dev)
{
  const char *str;

  g_return_val_if_fail (BOLT_IS_DEVICE (dev), NULL);

  str = bolt_proxy_get_string_by_pspec (dev, props[PROP_NAME]);

  return str;
}

const char *
bolt_device_get_vendor (BoltDevice *dev)
{
  const char *str;

  g_return_val_if_fail (BOLT_IS_DEVICE (dev), NULL);

  str = bolt_proxy_get_string_by_pspec (dev, props[PROP_VENDOR]);

  return str;
}

guint
bolt_device_get_generation (BoltDevice *dev)
{
  guint32 val = 0;

  g_return_val_if_fail (BOLT_IS_DEVICE (dev), 0);

  val = bolt_proxy_get_uint32_by_pspec (dev, props[PROP_GEN]);

  return (guint) val;
}

BoltDeviceType
bolt_device_get_device_type (BoltDevice *dev)
{
  gint val = BOLT_DEVICE_PERIPHERAL;

  g_return_val_if_fail (BOLT_IS_DEVICE (dev), val);

  val = bolt_proxy_get_enum_by_pspec (dev, props[PROP_TYPE]);

  return val;
}

gboolean
bolt_device_is_host (BoltDevice *dev)
{
  BoltDeviceType dt = bolt_device_get_device_type (dev);

  return dt == BOLT_DEVICE_HOST;
}

BoltStatus
bolt_device_get_status (BoltDevice *dev)
{
  gint val = BOLT_STATUS_UNKNOWN;

  g_return_val_if_fail (BOLT_IS_DEVICE (dev), val);

  val = bolt_proxy_get_enum_by_pspec (dev, props[PROP_STATUS]);

  return val;
}

BoltAuthFlags
bolt_device_get_authflags (BoltDevice *dev)
{
  guint val = BOLT_AUTH_NONE;

  g_return_val_if_fail (BOLT_IS_DEVICE (dev), val);

  val = bolt_proxy_get_flags_by_pspec (dev, props[PROP_AUTHFLAGS]);

  return val;
}

const char *
bolt_device_get_parent (BoltDevice *dev)
{
  const char *str;

  g_return_val_if_fail (BOLT_IS_DEVICE (dev), NULL);

  str = bolt_proxy_get_string_by_pspec (dev, props[PROP_PARENT]);

  return str;
}

const char *
bolt_device_get_syspath (BoltDevice *dev)
{
  const char *str;

  g_return_val_if_fail (BOLT_IS_DEVICE (dev), NULL);

  str = bolt_proxy_get_string_by_pspec (dev, props[PROP_SYSPATH]);

  return str;
}

const char *
bolt_device_get_domain (BoltDevice *dev)
{
  const char *str;

  g_return_val_if_fail (BOLT_IS_DEVICE (dev), NULL);

  str = bolt_proxy_get_string_by_pspec (dev, props[PROP_DOMAIN]);

  return str;
}
guint64
bolt_device_get_conntime (BoltDevice *dev)
{
  guint64 val = 0;

  g_return_val_if_fail (BOLT_IS_DEVICE (dev), val);

  val = bolt_proxy_get_uint64_by_pspec (dev, props[PROP_CONNTIME]);

  return val;
}

guint64
bolt_device_get_authtime (BoltDevice *dev)
{
  guint64 val = 0;

  g_return_val_if_fail (BOLT_IS_DEVICE (dev), val);

  val = bolt_proxy_get_uint64_by_pspec (dev, props[PROP_AUTHTIME]);

  return val;
}

void
bolt_device_get_linkspeed (BoltDevice    *dev,
                           BoltLinkSpeed *speed)
{
  g_auto(GValue) value = G_VALUE_INIT;
  BoltLinkSpeed *ls;
  gboolean ok;

  g_return_if_fail (BOLT_IS_DEVICE (dev));
  g_return_if_fail (speed != NULL);

  ok = bolt_proxy_get_dbus_property (BOLT_PROXY (dev),
                                     props[PROP_LINKSPEED],
                                     &value);
  if (!ok)
    return;

  ls = g_value_get_boxed (&value);
  *speed = *ls;
}

gboolean
bolt_device_is_stored (BoltDevice *dev)
{
  gboolean val = FALSE;

  g_return_val_if_fail (BOLT_IS_DEVICE (dev), val);

  val = bolt_proxy_get_bool_by_pspec (dev, props[PROP_STORED]);

  return val;
}

BoltPolicy
bolt_device_get_policy (BoltDevice *dev)
{
  gint val = BOLT_POLICY_DEFAULT;

  g_return_val_if_fail (BOLT_IS_DEVICE (dev), val);

  val = bolt_proxy_get_enum_by_pspec (dev, props[PROP_POLICY]);

  return val;
}

BoltKeyState
bolt_device_get_keystate (BoltDevice *dev)
{
  gint val = BOLT_KEY_MISSING;

  g_return_val_if_fail (BOLT_IS_DEVICE (dev), val);

  val = bolt_proxy_get_enum_by_pspec (dev, props[PROP_KEY]);

  return val;
}

guint64
bolt_device_get_storetime (BoltDevice *dev)
{
  guint64 val = 0;

  g_return_val_if_fail (BOLT_IS_DEVICE (dev), val);

  val = bolt_proxy_get_uint64_by_pspec (dev, props[PROP_STORETIME]);

  return val;
}

const char *
bolt_device_get_label (BoltDevice *dev)
{
  const char *str;

  g_return_val_if_fail (BOLT_IS_DEVICE (dev), NULL);

  str = bolt_proxy_get_string_by_pspec (dev, props[PROP_LABEL]);

  return str;
}

char *
bolt_device_get_display_name (BoltDevice *dev)
{
  const char *label;
  const char *name;
  const char *vendor;

  label = bolt_device_get_label (dev);
  if (label != NULL)
    return g_strdup (label);

  name = bolt_device_get_name (dev);
  vendor = bolt_device_get_vendor (dev);

  return g_strdup_printf ("%s %s", vendor, name);
}

guint64
bolt_device_get_timestamp (BoltDevice *dev)
{
  BoltStatus status;
  guint64 timestamp = 0;

  status = bolt_device_get_status (dev);

  switch (status)
    {
    case BOLT_STATUS_AUTHORIZING:
    case BOLT_STATUS_AUTH_ERROR:
    case BOLT_STATUS_CONNECTING:
    case BOLT_STATUS_CONNECTED:
      timestamp = bolt_device_get_conntime (dev);
      break;

    case BOLT_STATUS_DISCONNECTED:
      /* implicit: device is stored */
      timestamp = bolt_device_get_storetime (dev);
      break;

    case BOLT_STATUS_AUTHORIZED:
    case BOLT_STATUS_AUTHORIZED_DPONLY:
    case BOLT_STATUS_AUTHORIZED_NEWKEY:
    case BOLT_STATUS_AUTHORIZED_SECURE:
      timestamp = bolt_device_get_authtime (dev);
      break;

    case BOLT_STATUS_UNKNOWN:
      timestamp = 0;
      break;
    }

  return timestamp;
}

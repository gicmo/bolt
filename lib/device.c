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


#include <gio/gio.h>
#include <glib.h>

#include <stdio.h>

#include "device.h"
#include "enums.h"

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GEnumClass, g_type_class_unref);

char *
tb_policy_to_string (TbPolicy policy)
{
  g_autoptr(GEnumClass) klass = NULL;
  GEnumValue *value;

  klass = g_type_class_ref (TB_TYPE_POLICY);
  value = g_enum_get_value (klass, policy);

  return g_strdup (value->value_nick);
}

TbPolicy
tb_policy_from_string (const char *str)
{
  g_autoptr(GEnumClass) klass = NULL;
  GEnumValue *value;

  if (str == NULL)
    return TB_POLICY_UNKNOWN;

  klass = g_type_class_ref (TB_TYPE_POLICY);
  value = g_enum_get_value_by_nick (klass, str);

  if (value == NULL)
    {
      g_warning ("Unknown device policy: %s", str);
      return TB_POLICY_UNKNOWN;
    }

  return value->value;
}

struct _TbDevice
{
  GObject object;

  /* db or udev */
  char *uid;

  guint vendor;
  char *vendor_name;

  guint device;
  char *device_name;

  /* current status (udev) */
  char       *sysfs;
  TbAuthLevel authorized;

  /* db */
  gboolean known;
  TbPolicy policy;
};

struct _TbDeviceClass
{
  GObjectClass parent_class;

  gpointer     padding[13];
};

enum { PROP_DEVICE_0,

       PROP_UID,

       PROP_ID,
       PROP_NAME,

       PROP_VENDOR_ID,
       PROP_VENDOR_NAME,

       PROP_SYSFS,
       PROP_AUTHORIZED,

       PROP_KNOWN,
       PROP_POLICY,

       PROP_DEVICE_LAST };

static GParamSpec *device_props[PROP_DEVICE_LAST] = {
  NULL,
};

G_DEFINE_TYPE (TbDevice, tb_device, G_TYPE_OBJECT);

static void
tb_device_finalize (GObject *object)
{
  TbDevice *dev = TB_DEVICE (object);

  g_free (dev->uid);
  g_free (dev->vendor_name);
  g_free (dev->device_name);
  g_free (dev->sysfs);

  G_OBJECT_CLASS (tb_device_parent_class)->finalize (object);
}

static void
tb_device_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  TbDevice *dev = TB_DEVICE (object);

  switch (prop_id)
    {

    case PROP_UID:
      g_value_set_string (value, dev->uid);
      break;

    case PROP_ID:
      g_value_set_uint (value, dev->device);
      break;

    case PROP_NAME:
      g_value_set_string (value, dev->device_name);
      break;

    case PROP_VENDOR_ID:
      g_value_set_uint (value, dev->vendor);
      break;

    case PROP_VENDOR_NAME:
      g_value_set_string (value, dev->vendor_name);
      break;

    case PROP_SYSFS:
      g_value_set_string (value, dev->sysfs);
      break;

    case PROP_AUTHORIZED:
      g_value_set_enum (value, dev->authorized);
      break;

    case PROP_KNOWN:
      g_value_set_boolean (value, dev->known);
      break;

    case PROP_POLICY:
      g_value_set_enum (value, dev->policy);
      break;

    default:
      g_assert_not_reached ();
    }
}

static void
tb_device_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  TbDevice *dev = TB_DEVICE (object);

  switch (prop_id)
    {
    case PROP_UID:
      dev->uid = g_value_dup_string (value);
      break;

    case PROP_ID:
      dev->device = g_value_get_uint (value);
      break;

    case PROP_NAME:
      dev->device_name = g_value_dup_string (value);
      break;

    case PROP_VENDOR_ID:
      dev->vendor = g_value_get_uint (value);
      break;

    case PROP_VENDOR_NAME:
      dev->vendor_name = g_value_dup_string (value);
      break;

    case PROP_SYSFS:
      dev->sysfs = g_value_dup_string (value);
      break;

    case PROP_AUTHORIZED:
      dev->authorized = g_value_get_enum (value);
      break;

    case PROP_KNOWN:
      dev->known = g_value_get_boolean (value);
      break;

    case PROP_POLICY:
      dev->policy = g_value_get_enum (value);
      break;

    default:
      g_assert_not_reached ();
    }
}

static void
tb_device_init (TbDevice *mns)
{
}

static void
tb_device_class_init (TbDeviceClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = tb_device_finalize;

  gobject_class->get_property = tb_device_get_property;
  gobject_class->set_property = tb_device_set_property;

  device_props[PROP_UID] = g_param_spec_string ("uid",
                                                NULL,
                                                NULL,
                                                NULL,
                                                G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);

  device_props[PROP_ID] = g_param_spec_uint ("device-id",
                                             NULL,
                                             NULL,
                                             0,
                                             G_MAXUINT16,
                                             0,
                                             G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);

  device_props[PROP_NAME] =
    g_param_spec_string ("device-name",
                         NULL,
                         NULL,
                         "",
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);

  device_props[PROP_VENDOR_ID] =
    g_param_spec_uint ("vendor-id",
                       NULL,
                       NULL,
                       0,
                       G_MAXUINT16,
                       0,
                       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);

  device_props[PROP_VENDOR_NAME] =
    g_param_spec_string ("vendor-name",
                         NULL,
                         NULL,
                         "",
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);

  device_props[PROP_SYSFS] =
    g_param_spec_string ("sysfs", NULL, NULL, "", G_PARAM_READWRITE | G_PARAM_STATIC_NAME);

  device_props[PROP_AUTHORIZED] =
    g_param_spec_enum ("authorized",
                       NULL, NULL,
                       TB_TYPE_AUTH_LEVEL,
                       TB_AUTH_LEVEL_UNKNOWN,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_NAME);

  device_props[PROP_KNOWN] =
    g_param_spec_boolean ("known", NULL, NULL, FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_NAME);

  device_props[PROP_POLICY] = g_param_spec_enum ("policy",
                                                 NULL,
                                                 NULL,
                                                 TB_TYPE_POLICY,
                                                 TB_POLICY_UNKNOWN,
                                                 G_PARAM_READWRITE | G_PARAM_STATIC_NAME);

  g_object_class_install_properties (gobject_class, PROP_DEVICE_LAST, device_props);
}

const char *
tb_device_get_uid (const TbDevice *device)
{
  return device->uid;
}

const char *
tb_device_get_name (const TbDevice *device)
{
  return device->device_name;
}

guint
tb_device_get_device_id (const TbDevice *device)
{
  return device->device;
}

const char *
tb_device_get_vendor_name (const TbDevice *device)
{
  return device->vendor_name;
}

guint
tb_device_get_vendor_id (const TbDevice *device)
{
  return device->vendor;
}

const char *
tb_device_get_sysfs_path (const TbDevice *device)
{
  return device->sysfs;
}

gint
tb_device_get_authorized (const TbDevice *device)
{
  return device->authorized;
}

gboolean
tb_device_in_store (const TbDevice *device)
{
  return device->known;
}

TbPolicy
tb_device_get_policy (const TbDevice *device)
{
  return device->policy;
}

GFile *
tb_device_get_sysfs_keyfile (const TbDevice *device)
{
  g_autoptr(GFile) base = NULL;

  if (device->sysfs == NULL)
    return NULL;

  base = g_file_new_for_path (device->sysfs);
  return g_file_get_child (base, "key");
}

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

#include "bolt-enums.h"
#include "bolt-error.h"

#include <gio/gio.h>

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GEnumClass, g_type_class_unref);


gboolean
bolt_enum_class_validate (GEnumClass *enum_class,
                          gint        value,
                          GError    **error)
{
  const char *name;
  gboolean oob;

  if (enum_class == NULL)
    {
      name = g_type_name_from_class ((GTypeClass *) enum_class);
      g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                   "could not determine enum class for '%s'",
                   name);

      return FALSE;
    }

  oob = value < enum_class->minimum || value > enum_class->maximum;

  if (oob)
    {
      name = g_type_name_from_class ((GTypeClass *) enum_class);
      g_set_error (error,  G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                   "enum value '%d' is out of bounds for '%s'",
                   value, name);
      return FALSE;
    }

  return TRUE;
}

gboolean
bolt_enum_validate (GType    enum_type,
                    gint     value,
                    GError **error)
{
  g_autoptr(GEnumClass) klass = g_type_class_ref (enum_type);
  return bolt_enum_class_validate (klass, value, error);
}

const char *
bolt_enum_to_string (GType    enum_type,
                     gint     value,
                     GError **error)
{
  g_autoptr(GEnumClass) klass = NULL;
  GEnumValue *ev;

  klass = g_type_class_ref (enum_type);

  if (!bolt_enum_class_validate (klass, value, error))
    return NULL;

  ev = g_enum_get_value (klass, value);
  return ev->value_nick;
}

gint
bolt_enum_from_string (GType       enum_type,
                       const char *string,
                       GError    **error)
{
  g_autoptr(GEnumClass) klass = NULL;
  GEnumValue *ev;

  klass = g_type_class_ref (enum_type);

  ev = g_enum_get_value_by_nick (klass, string);

  if (ev == NULL)
    {
      const char *name = g_type_name (enum_type);

      g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                   "invalid str '%s' for enum '%s'", string, name);
      return -1;
    }

  return ev->value;
}

const char *
bolt_status_to_string (BoltStatus status)
{
  return bolt_enum_to_string (BOLT_TYPE_STATUS, status, NULL);
}

gboolean
bolt_status_is_authorized (BoltStatus status)
{
  return status == BOLT_STATUS_AUTHORIZED ||
         status == BOLT_STATUS_AUTHORIZED_SECURE ||
         status == BOLT_STATUS_AUTHORIZED_NEWKEY;
}

gboolean
bolt_status_validate (BoltStatus status)
{
  return bolt_enum_validate (BOLT_TYPE_STATUS, status, NULL);
}

gboolean
bolt_status_is_connected (BoltStatus status)
{
  return status > BOLT_STATUS_DISCONNECTED;
}

BoltSecurity
bolt_security_from_string (const char *str)
{
  g_autoptr(GEnumClass) klass = NULL;
  GEnumValue *value;

  if (str == NULL)
    return BOLT_SECURITY_INVALID;

  klass = g_type_class_ref (BOLT_TYPE_SECURITY);
  value = g_enum_get_value_by_nick (klass, str);

  if (value == NULL)
    return BOLT_SECURITY_INVALID;

  return value->value;
}

const char *
bolt_security_to_string (BoltSecurity security)
{
  g_autoptr(GEnumClass) klass = NULL;
  GEnumValue *value;

  if (!bolt_security_validate (security))
    return NULL;

  klass = g_type_class_ref (BOLT_TYPE_SECURITY);
  value = g_enum_get_value (klass, security);

  return value->value_nick;
}

gboolean
bolt_security_validate (BoltSecurity security)
{
  return security < BOLT_SECURITY_INVALID && security >= 0;
}

BoltPolicy
bolt_policy_from_string (const char *str)
{
  g_autoptr(GEnumClass) klass = NULL;
  GEnumValue *value;

  if (str == NULL)
    return BOLT_POLICY_AUTO;

  klass = g_type_class_ref (BOLT_TYPE_POLICY);
  value = g_enum_get_value_by_nick (klass, str);

  if (value == NULL)
    return BOLT_POLICY_INVALID;

  return value->value;
}

const char *
bolt_policy_to_string (BoltPolicy policy)
{
  g_autoptr(GEnumClass) klass = NULL;
  GEnumValue *value;

  if (!bolt_policy_validate (policy))
    return NULL;

  klass = g_type_class_ref (BOLT_TYPE_POLICY);
  value = g_enum_get_value (klass, policy);

  return value->value_nick;
}

gboolean
bolt_policy_validate (BoltPolicy policy)
{
  return policy < BOLT_POLICY_INVALID && policy >= 0;
}

BoltDeviceType
bolt_device_type_from_string (const char *str)
{
  g_autoptr(GEnumClass) klass = NULL;
  GEnumValue *value;

  if (str == NULL)
    return BOLT_DEVICE_PERIPHERAL;

  klass = g_type_class_ref (BOLT_TYPE_DEVICE_TYPE);
  value = g_enum_get_value_by_nick (klass, str);

  if (value == NULL)
    return BOLT_DEVICE_TYPE_INVALID;

  return value->value;
}

const char *
bolt_device_type_to_string (BoltDeviceType type)
{
  g_autoptr(GEnumClass) klass = NULL;
  GEnumValue *value;

  if (!bolt_device_type_validate (type))
    return NULL;

  klass = g_type_class_ref (BOLT_TYPE_DEVICE_TYPE);
  value = g_enum_get_value (klass, type);

  return value->value_nick;
}

gboolean
bolt_device_type_validate (BoltDeviceType type)
{
  return type < BOLT_DEVICE_TYPE_INVALID && type >= 0;
}

gboolean
bolt_device_type_is_host (BoltDeviceType type)
{
  return type == BOLT_DEVICE_HOST;
}

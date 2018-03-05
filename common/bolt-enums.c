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
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GFlagsClass, g_type_class_unref);

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

char *
bolt_flags_class_to_string (GFlagsClass *flags_class,
                            guint        value,
                            GError     **error)
{
  g_autoptr(GString) str = NULL;
  const char *name;
  GFlagsValue *fv;

  if (flags_class == NULL)
    {
      name = g_type_name_from_class ((GTypeClass *) flags_class);
      g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                   "could not determine flags class for '%s'",
                   name);

      return FALSE;
    }

  fv = g_flags_get_first_value (flags_class, value);
  if (fv == NULL)
    {
      if (value == 0)
        return g_strdup ("");

      name = g_type_name_from_class ((GTypeClass *) flags_class);

      g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                   "invalid value '%u' for flags '%s'", value, name);
      return NULL;
    }

  value &= ~fv->value;
  str = g_string_new (fv->value_nick);

  while (value != 0 &&
         (fv = g_flags_get_first_value (flags_class, value)) != NULL)
    {
      g_string_append (str, " | ");
      g_string_append (str, fv->value_nick);

      value &= ~fv->value;
    }

  if (value != 0)
    {
      name = g_type_name_from_class ((GTypeClass *) flags_class);

      g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                   "unhandled value '%u' for flags '%s'", value, name);
      return NULL;
    }

  return g_string_free (g_steal_pointer (&str), FALSE);
}

gboolean
bolt_flags_class_from_string (GFlagsClass *flags_class,
                              const char  *string,
                              guint       *flags_out,
                              GError     **error)
{
  g_auto(GStrv) vals = NULL;
  const char *name;
  guint flags = 0;

  if (flags_class == NULL)
    {
      name = g_type_name_from_class ((GTypeClass *) flags_class);
      g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                   "could not determine enum class for '%s'",
                   name);

      return FALSE;
    }

  vals = g_strsplit (string, "|", -1);

  for (guint i = 0; vals[i]; i++)
    {
      GFlagsValue *fv;
      char *nick;

      nick = g_strstrip (vals[i]);
      fv = g_flags_get_value_by_nick (flags_class, nick);

      if (fv == NULL)
        {
          name = g_type_name_from_class ((GTypeClass *) flags_class);
          g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                       "invalid flag '%s' for flags '%s'", string, name);

          return FALSE;
        }

      flags |= fv->value;
    }

  if (flags_out != NULL)
    *flags_out = flags;

  return TRUE;
}

char *
bolt_flags_to_string (GType    flags_type,
                      guint    value,
                      GError **error)
{
  g_autoptr(GFlagsClass) klass = NULL;

  klass = g_type_class_ref (flags_type);
  return bolt_flags_class_to_string (klass, value, error);
}

gboolean
bolt_flags_from_string (GType       flags_type,
                        const char *string,
                        guint      *flags_out,
                        GError    **error)
{
  g_autoptr(GFlagsClass) klass = NULL;

  klass = g_type_class_ref (flags_type);
  return bolt_flags_class_from_string (klass, string, flags_out, error);
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
  return bolt_enum_from_string (BOLT_TYPE_POLICY, str, NULL);
}

const char *
bolt_policy_to_string (BoltPolicy policy)
{
  return bolt_enum_to_string (BOLT_TYPE_POLICY, policy, NULL);
}

gboolean
bolt_policy_validate (BoltPolicy policy)
{
  return bolt_enum_validate (BOLT_TYPE_POLICY, policy, NULL);
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

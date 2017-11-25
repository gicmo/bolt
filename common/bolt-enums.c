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

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GEnumClass, g_type_class_unref);

const char *
bolt_status_to_string (BoltStatus status)
{
  g_autoptr(GEnumClass) klass = NULL;
  GEnumValue *value;

  if (!bolt_status_validate (status))
    return NULL;

  klass = g_type_class_ref (BOLT_TYPE_STATUS);
  value = g_enum_get_value (klass, status);

  return value->value_nick;
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
  return status < BOLT_STATUS_INVALID && status >= 0;
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

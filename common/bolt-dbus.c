/*
 * Copyright © 2019 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
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
#include "bolt-error.h"

#include "bolt-dbus-resource.h"
#include "bolt-str.h"

static gpointer
register_dbus_resources (gpointer data)
{
  g_resources_register (bolt_dbus_get_resource ());
  return NULL;
}

void
bolt_dbus_ensure_resources (void)
{
  static GOnce only_once = G_ONCE_INIT;

  g_once (&only_once, register_dbus_resources, NULL);
}

GDBusInterfaceInfo *
bolt_dbus_interface_info_find (const char *interface_xml,
                               const char *interface_name,
                               GError    **error)
{
  g_autoptr(GDBusNodeInfo) node = NULL;
  GDBusInterfaceInfo **iter;
  GDBusInterfaceInfo *info = NULL;

  g_return_val_if_fail (interface_xml != NULL, NULL);
  g_return_val_if_fail (interface_name != NULL, NULL);

  node = g_dbus_node_info_new_for_xml (interface_xml, error);
  if (node == NULL)
    return NULL;

  for (iter = node->interfaces; iter && *iter; iter++)
    {
      GDBusInterfaceInfo *ii = *iter;
      if (bolt_streq (ii->name, interface_name))
        {
          info = ii;
          break;
        }
    }

  if (info == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "could not find interface with name '%s",
                   interface_name);
      return NULL;
    }

  return g_dbus_interface_info_ref (info);
}

GDBusInterfaceInfo *
bolt_dbus_interface_info_lookup (const char *resource_name,
                                 const char *interface_name,
                                 GError    **error)
{
  g_autoptr(GBytes) data = NULL;
  GDBusInterfaceInfo *info;
  const char *xml;

  g_return_val_if_fail (resource_name != NULL, NULL);
  g_return_val_if_fail (interface_name != NULL, NULL);

  data = g_resources_lookup_data (resource_name,
                                  G_RESOURCE_LOOKUP_FLAGS_NONE,
                                  error);

  if (data == NULL)
    return NULL;

  xml = g_bytes_get_data (data, NULL);
  info = bolt_dbus_interface_info_find (xml, interface_name, error);

  return info;
}

gboolean
bolt_dbus_get_sender_pid (GDBusMethodInvocation *invocation,
                          guint                 *pid,
                          GError               **error)
{
  g_autoptr(GVariant) res = NULL;
  g_autoptr(GError) err = NULL;
  GDBusConnection *con;
  const char *sender;

  con = g_dbus_method_invocation_get_connection (invocation);
  sender = g_dbus_method_invocation_get_sender (invocation);

  res = g_dbus_connection_call_sync (con,
                                     "org.freedesktop.DBus",
                                     "/",
                                     "org.freedesktop.DBus",
                                     "GetConnectionUnixProcessID",
                                     g_variant_new ("(s)", sender),
                                     G_VARIANT_TYPE ("(u)"),
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1, NULL,
                                     &err);
  if (res == NULL)
    {
      g_set_error (error, BOLT_ERROR, BOLT_ERROR_FAILED,
                   "could not get pid of caller: %s",
                   err->message);
      return FALSE;
    }

  g_variant_get (res, "(u)", pid);

  return TRUE;
}

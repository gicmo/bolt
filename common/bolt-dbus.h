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
#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

void                   bolt_dbus_ensure_resources (void);

GDBusInterfaceInfo *   bolt_dbus_interface_info_find (const char *interface_xml,
                                                      const char *interface_name,
                                                      GError    **error);

GDBusInterfaceInfo *   bolt_dbus_interface_info_lookup (const char *resource_name,
                                                        const char *interface_name,
                                                        GError    **error);

gboolean               bolt_dbus_get_sender_pid (GDBusMethodInvocation *invocation,
                                                 guint                 *pid,
                                                 GError               **error);
G_END_DECLS

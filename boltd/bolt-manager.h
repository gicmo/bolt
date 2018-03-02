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

#pragma once

#include "bolt-exported.h"

G_BEGIN_DECLS

#define BOLT_TYPE_MANAGER bolt_manager_get_type ()
G_DECLARE_FINAL_TYPE (BoltManager, bolt_manager, BOLT, MANAGER, BoltExported);

gboolean         bolt_manager_export (BoltManager     *mgr,
                                      GDBusConnection *connection,
                                      GError         **error);

void             bolt_manager_got_the_name (BoltManager *mgr);

G_END_DECLS

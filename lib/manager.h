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


#ifndef __TB_MANAGER_H__
#define __TB_MANAGER_H__

#include <glib-object.h>

#include "device.h"

G_BEGIN_DECLS
#define TB_TYPE_MANAGER tb_manager_get_type ()
G_DECLARE_FINAL_TYPE (TbManager, tb_manager, TB, MANAGER, GObject);

TbManager *tb_manager_new (GError **error);

const GPtrArray *tb_manager_list_attached (TbManager *mgr);

TbDevice *tb_manager_lookup (TbManager  *mgr,
                             const char *uid);

gboolean tb_manager_store (TbManager *mgr,
                           TbDevice  *device,
                           GError   **error);

GFile *tb_manager_ensure_key (TbManager *mgr,
                              TbDevice  *dev,
                              gboolean   replace,
                              gboolean * created,
                              GError   **error);
G_END_DECLS
#endif /* TB_MANAGER_H */

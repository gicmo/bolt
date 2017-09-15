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


#ifndef __TB_STORE_H__
#define __TB_STORE_H__

#include <glib-object.h>

#include "device.h"

G_BEGIN_DECLS
#define TB_TYPE_STORE tb_store_get_type ()
G_DECLARE_FINAL_TYPE (TbStore, tb_store, TB, STORE, GObject);

#define TB_KEY_BYTES 32
#define TB_KEY_CHARS 64

TbStore     *tb_store_new (const char *path);
gboolean     tb_store_have (TbStore    *store,
                            const char *uid);
gboolean     tb_store_put (TbStore  *store,
                           TbDevice *device,
                           GError  **error);
int          tb_store_create_key (TbStore  *store,
                                  TbDevice *device,
                                  GError  **error);
gint         tb_store_open_key (TbStore    *store,
                                const char *uid,
                                GError    **error);
gboolean     tb_store_have_key (TbStore    *store,
                                const char *uid);
gboolean     tb_store_merge (TbStore  *store,
                             TbDevice *dev,
                             GError  **error);
TbDevice    *tb_store_get (TbStore    *store,
                           const char *uid,
                           GError    **error);
gboolean     tb_store_delete (TbStore    *store,
                              const char *uid,
                              GError    **error);
GStrv        tb_store_list_ids (TbStore *store,
                                GError **error);

G_END_DECLS
#endif /* TB_STORE_H */

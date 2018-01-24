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

#include <glib-object.h>

#include "bolt-device.h"
#include "bolt-key.h"
#include "bolt-enums.h"

G_BEGIN_DECLS


/* BoltStore - database for devices, keys */
#define BOLT_TYPE_STORE bolt_store_get_type ()
G_DECLARE_FINAL_TYPE (BoltStore, bolt_store, BOLT, STORE, GObject);

BoltStore *       bolt_store_new (const char *path);

GKeyFile *        bolt_store_config_load (BoltStore *store,
                                          GError   **error);

gboolean          bolt_store_config_save (BoltStore *store,
                                          GKeyFile  *config,
                                          GError   **error);

GStrv             bolt_store_list_uids (BoltStore *store,
                                        GError   **error);

gboolean          bolt_store_del (BoltStore  *store,
                                  BoltDevice *dev,
                                  GError    **error);

gboolean          bolt_store_put_device (BoltStore  *store,
                                         BoltDevice *device,
                                         BoltPolicy  policy,
                                         BoltKey    *key,
                                         GError    **error);

BoltDevice *      bolt_store_get_device (BoltStore  *store,
                                         const char *uid,
                                         GError    **error);

gboolean          bolt_store_del_device (BoltStore  *store,
                                         const char *uid,
                                         GError    **error);

BoltKeyState      bolt_store_have_key (BoltStore  *store,
                                       const char *uid);

BoltKey *         bolt_store_get_key (BoltStore  *store,
                                      const char *uid,
                                      GError    **error);

gboolean          bolt_store_del_key (BoltStore  *store,
                                      const char *uid,
                                      GError    **error);

G_END_DECLS

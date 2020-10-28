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
#include "bolt-domain.h"
#include "bolt-enums.h"
#include "bolt-key.h"
#include "bolt-journal.h"

G_BEGIN_DECLS

#define BOLT_STORE_VERSION 1

/* BoltStore - database for devices, keys */
#define BOLT_TYPE_STORE bolt_store_get_type ()
G_DECLARE_FINAL_TYPE (BoltStore, bolt_store, BOLT, STORE, GObject);

BoltStore *       bolt_store_new (const char *path,
                                  GError    **error);

guint             bolt_store_get_version (BoltStore *store);

GKeyFile *        bolt_store_config_load (BoltStore *store,
                                          GError   **error);

gboolean          bolt_store_config_save (BoltStore *store,
                                          GKeyFile  *config,
                                          GError   **error);

GStrv             bolt_store_list_uids (BoltStore  *store,
                                        const char *type,
                                        GError    **error);

gboolean          bolt_store_put_domain (BoltStore  *store,
                                         BoltDomain *domain,
                                         GError    **error);

BoltDomain *      bolt_store_get_domain (BoltStore  *store,
                                         const char *uid,
                                         GError    **error);

gboolean          bolt_store_del_domain (BoltStore  *store,
                                         BoltDomain *domain,
                                         GError    **error);

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

gboolean          bolt_store_put_time (BoltStore  *store,
                                       const char *uid,
                                       const char *timesel,
                                       guint64     val,
                                       GError    **error);

gboolean          bolt_store_put_times (BoltStore  *store,
                                        const char *uid,
                                        GError    **error,
                                        ...) G_GNUC_NULL_TERMINATED;

gboolean          bolt_store_get_time (BoltStore  *store,
                                       const char *uid,
                                       const char *timesel,
                                       guint64    *outval,
                                       GError    **error);

gboolean          bolt_store_get_times (BoltStore  *store,
                                        const char *uid,
                                        GError    **error,
                                        ...) G_GNUC_NULL_TERMINATED;

gboolean          bolt_store_del_time (BoltStore  *store,
                                       const char *uid,
                                       const char *timesel,
                                       GError    **error);

gboolean          bolt_store_del_times (BoltStore  *store,
                                        const char *uid,
                                        GError    **error,
                                        ...) G_GNUC_NULL_TERMINATED;

gboolean          bolt_store_put_key (BoltStore  *store,
                                      const char *uid,
                                      BoltKey    *key,
                                      GError    **error);

BoltKeyState      bolt_store_have_key (BoltStore  *store,
                                       const char *uid);

BoltKey *         bolt_store_get_key (BoltStore  *store,
                                      const char *uid,
                                      GError    **error);

gboolean          bolt_store_del_key (BoltStore  *store,
                                      const char *uid,
                                      GError    **error);

BoltJournal *     bolt_store_open_journal (BoltStore  *store,
                                           const char *type,
                                           const char *name,
                                           GError    **error);

gboolean          bolt_store_del_journal (BoltStore  *store,
                                          const char *type,
                                          const char *name,
                                          GError    **error);

gboolean          bolt_store_has_journal (BoltStore  *store,
                                          const char *type,
                                          const char *name);

/* store upgrades */
gboolean          bolt_store_upgrade (BoltStore *store,
                                      gboolean  *upgrade,
                                      GError   **error);

G_END_DECLS

/*
 * Copyright © 2018 Red Hat, Inc
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

#include "bolt-enums.h"
#include "bolt-exported.h"

#include <gio/gio.h>

G_BEGIN_DECLS

/* forward declaration */
struct udev_device;

#define BOLT_TYPE_DOMAIN bolt_domain_get_type ()
G_DECLARE_FINAL_TYPE (BoltDomain, bolt_domain, BOLT, DOMAIN, BoltExported);

BoltDomain *      bolt_domain_new_for_udev (struct udev_device *udev,
                                            const char         *uid,
                                            GError            **error) G_GNUC_WARN_UNUSED_RESULT;

const char *      bolt_domain_get_uid (BoltDomain *domain);

const char *      bolt_domain_get_id (BoltDomain *domain);

const char *      bolt_domain_get_syspath (BoltDomain *domain);

BoltSecurity      bolt_domain_get_security (BoltDomain *domain);

GStrv             bolt_domain_get_bootacl (BoltDomain *domain);

GStrv             bolt_domain_dup_bootacl (BoltDomain *domain);

gboolean          bolt_domain_is_stored (BoltDomain *domain);

gboolean          bolt_domain_is_connected (BoltDomain *domain);

gboolean          bolt_domain_has_iommu (BoltDomain *domain);

void              bolt_domain_export (BoltDomain      *domain,
                                      GDBusConnection *connection);

void              bolt_domain_connected (BoltDomain         *domain,
                                         struct udev_device *udev);

void              bolt_domain_disconnected (BoltDomain *domain);

void              bolt_domain_update_from_udev (BoltDomain         *domain,
                                                struct udev_device *udev);

gboolean          bolt_domain_can_delete (BoltDomain *domain,
                                          GError    **error);

/* boot acl related functions */
gboolean          bolt_domain_supports_bootacl (BoltDomain *domain);

guint             bolt_domain_bootacl_slots (BoltDomain *domain,
                                             guint      *n_free);

gboolean          bolt_domain_bootacl_contains (BoltDomain *domain,
                                                const char *uuid);

const char **     bolt_domain_bootacl_get_used (BoltDomain *domain,
                                                guint      *n_used);

void              bolt_domain_bootacl_allocate (BoltDomain *domain,
                                                GStrv       acl,
                                                const char *uuid);
gboolean          bolt_domain_bootacl_set (BoltDomain *domain,
                                           GStrv       acl,
                                           GError    **error);

gboolean          bolt_domain_bootacl_add (BoltDomain *domain,
                                           const char *uuid,
                                           GError    **error);

gboolean          bolt_domain_bootacl_del (BoltDomain *domain,
                                           const char *uuid,
                                           GError    **error);
/* domain list management */
BoltDomain *      bolt_domain_insert (BoltDomain *list,
                                      BoltDomain *domain) G_GNUC_WARN_UNUSED_RESULT;

BoltDomain *      bolt_domain_remove (BoltDomain *list,
                                      BoltDomain *domain) G_GNUC_WARN_UNUSED_RESULT;

BoltDomain *      bolt_domain_next (BoltDomain *domain);

BoltDomain *      bolt_domain_prev (BoltDomain *domain);

guint             bolt_domain_count (BoltDomain *domain);

void              bolt_domain_foreach (BoltDomain *list,
                                       GFunc       func,
                                       gpointer    user_data);

BoltDomain *      bolt_domain_find_id (BoltDomain *list,
                                       const char *id,
                                       GError    **error);

void              bolt_domain_clear (BoltDomain **list);


G_END_DECLS

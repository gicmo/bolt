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
                                            GError            **error) G_GNUC_WARN_UNUSED_RESULT;

const char *      bolt_domain_get_id (BoltDomain *domain);

const char *      bolt_domain_get_syspath (BoltDomain *domain);

BoltSecurity      bolt_domain_get_security (BoltDomain *domain);

void              bolt_domain_export (BoltDomain      *domain,
                                      GDBusConnection *connection);

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

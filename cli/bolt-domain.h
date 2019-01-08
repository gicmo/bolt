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
#include "bolt-proxy.h"

G_BEGIN_DECLS

#define BOLT_TYPE_DOMAIN bolt_domain_get_type ()
G_DECLARE_FINAL_TYPE (BoltDomain, bolt_domain, BOLT, DOMAIN, BoltProxy);


BoltDomain *      bolt_domain_new_for_object_path (GDBusConnection *bus,
                                                   const char      *path,
                                                   GCancellable    *cancellable,
                                                   GError         **error);
/* getter */
const char *      bolt_domain_get_uid (BoltDomain *domain);

const char *      bolt_domain_get_id (BoltDomain *domain);

const char *      bolt_domain_get_syspath (BoltDomain *domain);

BoltSecurity      bolt_domain_get_security (BoltDomain *domain);

char **           bolt_domain_get_bootacl (BoltDomain *domain);

gboolean          bolt_domain_is_online (BoltDomain *domain);

gboolean          bolt_domain_has_iommu (BoltDomain *domain);

G_END_DECLS

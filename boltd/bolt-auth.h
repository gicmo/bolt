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

#include "bolt-enums.h"
#include "bolt-key.h"

#include <gio/gio.h>

G_BEGIN_DECLS

/* forward decl because bolt-device.h include bolt-auth.h */
typedef struct _BoltDevice BoltDevice;

#define BOLT_TYPE_AUTH bolt_auth_get_type ()
G_DECLARE_FINAL_TYPE (BoltAuth, bolt_auth, BOLT, AUTH, GObject);

BoltAuth *       bolt_auth_new (gpointer     origin,
                                BoltSecurity level,
                                BoltKey     *key);

void             bolt_auth_return_new_error (BoltAuth   *auth,
                                             GQuark      domain,
                                             gint        code,
                                             const char *format,
                                             ...) G_GNUC_PRINTF (4, 5);

void             bolt_auth_return_error (BoltAuth *auth,
                                         GError  **error);

gboolean         bolt_auth_check (BoltAuth *auth,
                                  GError  **error);

BoltDevice *     bolt_auth_get_device (BoltAuth *auth);

BoltSecurity     bolt_auth_get_level (BoltAuth *auth);

BoltKey *        bolt_auth_get_key (BoltAuth *auth);

BoltKeyState     bolt_auth_get_keystate (BoltAuth *auth);

gboolean         bolt_auth_has_key (BoltAuth *auth);

gpointer         bolt_auth_get_origin (BoltAuth *auth);

BoltPolicy       bolt_auth_get_policy (BoltAuth *auth);

void             bolt_auth_set_policy (BoltAuth  *auth,
                                       BoltPolicy policy);

BoltStatus       bolt_auth_to_status (BoltAuth *auth);

BoltAuthFlags    bolt_auth_to_flags (BoltAuth      *auth,
                                     BoltAuthFlags *mask);


G_END_DECLS

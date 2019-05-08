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

#include <gio/gio.h>

#include "bolt-enums.h"

G_BEGIN_DECLS

/* BoltKey - represents a key to authorize devices with */
#define BOLT_TYPE_KEY bolt_key_get_type ()
G_DECLARE_FINAL_TYPE (BoltKey, bolt_key, BOLT, KEY, GObject);

#define BOLT_KEY_BYTES 32
#define BOLT_KEY_CHARS 64

BoltKey  *        bolt_key_new (GError **error);

gboolean          bolt_key_write_to (BoltKey      *key,
                                     int           fd,
                                     BoltSecurity *level,
                                     GError      **error);

gboolean          bolt_key_save_file (BoltKey *key,
                                      GFile   *file,
                                      GError **error);

BoltKey *         bolt_key_load_file (GFile   *file,
                                      GError **error);

BoltKeyState      bolt_key_get_state (BoltKey *key);

G_END_DECLS

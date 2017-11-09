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

#include <glib.h>

G_BEGIN_DECLS

/* general function */
typedef enum {
  BOLT_RNG_ERROR = -1,
  BOLT_RNG_URANDOM = 1,
  BOLT_RNG_PRNG = 2,
  BOLT_RNG_GETRANDOM = 3,
} BoltRng;

BoltRng  bolt_get_random_data (void *buf,
                               gsize n);

/* specific implementations */
gboolean bolt_random_getrandom (void    *buf,
                                gsize    n,
                                unsigned flags,
                                GError **error);
gboolean bolt_random_urandom (void *buf,
                              gsize n);
void     bolt_random_prng (void *buf,
                           gsize n);

G_END_DECLS

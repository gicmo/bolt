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

#include <glib.h>

#include "bolt-enums.h"

G_BEGIN_DECLS

/* well known paths */
const char *   bolt_get_store_path (void);

const char *   bolt_get_runtime_directory (void);

/* user and system configuration */
typedef enum BoltTri {

  TRI_ERROR = -1,
  TRI_NO    =  0,
  TRI_YES   =  1,

} BoltTri;

GKeyFile * bolt_config_user_init (void);


BoltTri    bolt_config_load_default_policy (GKeyFile   *cfg,
                                            BoltPolicy *policy,
                                            GError    **error);

BoltTri   bolt_config_load_auth_mode (GKeyFile     *cfg,
                                      BoltAuthMode *authmode,
                                      GError      **error);

void      bolt_config_set_auth_mode (GKeyFile   *cfg,
                                     const char *authmode);

G_END_DECLS

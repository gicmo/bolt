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

#include "bolt-error.h"

#include <gio/gio.h>

G_BEGIN_DECLS

gboolean     bolt_fs_make_parent_dirs (GFile   *target,
                                       GError **error);

gboolean     bolt_fs_cleanup_dir (const char *target,
                                  GError    **error);

gboolean     bolt_fs_touch (GFile   *target,
                            guint64  atime,
                            guint64  mtime,
                            GError **error);

G_END_DECLS

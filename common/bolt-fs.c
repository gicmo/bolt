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

#include "config.h"

#include "bolt-error.h"
#include "bolt-fs.h"

gboolean
bolt_fs_make_parent_dirs (GFile   *target,
                          GError **error)
{
  g_autoptr(GError) err = NULL;
  g_autoptr(GFile) p = NULL;
  gboolean ok;

  p = g_file_get_parent (target);

  ok = g_file_make_directory_with_parents (p, NULL, &err);
  if (!ok && !bolt_err_exists (err))
    {
      g_propagate_error (error, g_steal_pointer (&err));
      return FALSE;
    }

  return TRUE;
}

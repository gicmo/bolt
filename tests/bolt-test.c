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

#include "config.h"

#include "bolt-test.h"

#include "bolt-fs.h"

BoltTmpDir
bolt_tmp_dir_make (const char *pattern,
                   GError    **error)
{
  g_autoptr(GError) err = NULL;
  BoltTmpDir dir = g_dir_make_tmp (pattern, &err);

  if (dir == NULL)
    {
      if (error)
        bolt_error_propagate (error, &err);
      else
        g_critical ("could not create tmp dir [%s]: %s",
                    pattern, err->message);
      return NULL;
    }

  g_debug ("tmp dir made at '%s'", dir);

  return dir;
}

void
bolt_tmp_dir_destroy (BoltTmpDir dir)
{
  g_autoptr(GError) err = NULL;
  gboolean ok;

  if (dir == NULL)
    return;

  g_debug ("cleaning tmp dir at '%s'", dir);
  ok = bolt_fs_cleanup_dir (dir, &err);

  if (!ok)
    g_warning ("could not clean up dir: %s", err->message);

  g_free (dir);
}

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
#include "bolt-io.h"
#include "bolt-fs.h"

#include <dirent.h>
#include <errno.h>

gboolean
bolt_fs_make_parent_dirs (GFile   *target,
                          GError **error)
{
  g_autoptr(GError) err = NULL;
  g_autoptr(GFile) p = NULL;
  gboolean ok;

  g_return_val_if_fail (G_IS_FILE (target), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  p = g_file_get_parent (target);

  ok = g_file_make_directory_with_parents (p, NULL, &err);
  if (!ok && !bolt_err_exists (err))
    return bolt_error_propagate (error, &err);

  return TRUE;
}

static void
cleanup_dir (DIR *d)
{
  struct dirent *de = NULL;

  while ((de = readdir (d)) != NULL)
    {
      g_autoptr(GError) error = NULL;
      int uflag = 0;

      if (!g_strcmp0 (de->d_name, ".") ||
          !g_strcmp0 (de->d_name, ".."))
        continue;

      if (de->d_type == DT_DIR)
        {
          DIR *cd;

          cd = bolt_opendir_at (dirfd (d), de->d_name, O_RDONLY, &error);
          if (cd == NULL)
            continue;

          cleanup_dir (cd);
          uflag = AT_REMOVEDIR;
          closedir (cd);
        }

      bolt_unlink_at (dirfd (d), de->d_name, uflag, NULL);
    }
}

gboolean
bolt_fs_cleanup_dir (const char *target,
                     GError    **error)
{
  DIR *d = NULL;

  g_return_val_if_fail (target != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  d = bolt_opendir (target, error);
  if (d == NULL)
    return FALSE;

  cleanup_dir (d);
  (void) bolt_closedir (d, NULL);

  return bolt_rmdir (target, error);
}

#define TOUCH_FLAGS O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC

static inline void
timespec_set_from_uint64 (struct timespec *to,
                          guint64          from)
{
  if (from > 0)
    {
      to->tv_sec = (time_t) from;
      to->tv_nsec = 0;
    }
  else
    {
      to->tv_sec = 0;
      to->tv_nsec = UTIME_OMIT;
    }
}

gboolean
bolt_fs_touch (GFile   *target,
               guint64  atime,
               guint64  mtime,
               GError **error)
{
  g_autofree char *path = NULL;
  struct timespec times[2];
  gboolean ok;
  int fd;
  int r;

  g_return_val_if_fail (target != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  path = g_file_get_path (target);

  fd = bolt_open (path, TOUCH_FLAGS, 0664, error);

  if (fd < 0)
    return FALSE;

  /* times[0] is the "last access time" (atime) */
  timespec_set_from_uint64 (&times[0], atime);

  /* times[1] is the "last modification time" (mtime). */
  timespec_set_from_uint64 (&times[1], mtime);

  r = futimens (fd, times);

  if (r == -1)
    {
      int errsave = errno;
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errsave),
                   "could not touch file: %s", g_strerror (errsave));
    }

  ok = bolt_close (fd, error);

  return r != -1 && ok;
}

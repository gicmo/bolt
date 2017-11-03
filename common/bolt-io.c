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

#include <gio/gio.h>

#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>

#include "bolt-error.h"

#include "bolt-io.h"

G_DEFINE_AUTOPTR_CLEANUP_FUNC (FILE, fclose);

int
bolt_open (const char *path, int flags, int mode, GError **error)
{
  int fd = g_open (path, flags, mode);

  if (fd < 0)
    {
      gint code = g_io_error_from_errno (errno);
      g_set_error (error, G_IO_ERROR, code,
                   "could not open '%s': %s",
                   path, g_strerror (errno));
      return -1;
    }

  return fd;
}

static FILE *
bolt_fdopen (int fd, const char *mode, GError **error)
{
  FILE *fp = fdopen (fd, mode);

  if (fp == NULL)
    {
      gint code = g_io_error_from_errno (errno);
      g_set_error (error, G_IO_ERROR, code,
                   "fdopen ('%d') error: %s",
                   fd, g_strerror (errno));
      return NULL;
    }

  return fp;
}

gboolean
bolt_close (int fd, GError **error)
{
  int r;

  r = close (fd);

  if (r == 0)
    return TRUE;

  g_set_error (error, G_IO_ERROR,
               g_io_error_from_errno (errno),
               "could not close file: %s",
               g_strerror (errno));

  return FALSE;
}

DIR *
bolt_opendir (const char *path,
              GError    **error)
{
  DIR *d = NULL;

  d = opendir (path);
  if (d == NULL)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_errno (errno),
                   "could not open directory ('%s'): %s",
                   path,
                   g_strerror (errno));

      return NULL;
    }

  return d;
}

int
bolt_openat (int dirfd, const char *path, int oflag, GError **error)
{
  int fd = -1;

  fd = openat (dirfd, path, oflag);

  if (fd < 0)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_errno (errno),
                   "Could not open file %s: %s",
                   path,
                   g_strerror (errno));
    }

  return fd;
}


DIR *
bolt_opendir_at (int         dirfd,
                 const char *name,
                 int         oflag,
                 GError    **error)
{
  int fd = -1;
  DIR *cd;

  fd = bolt_openat (dirfd, name, oflag, error);
  if (fd < 0)
    return NULL;

  cd = fdopendir (fd);
  if (cd == NULL)
    {
      g_set_error (error, G_IO_ERROR,
                   g_io_error_from_errno (errno),
                   "failed to open directory: %s",
                   g_strerror (errno));

      (void) close (fd);
    }

  return cd;
}


char *
bolt_read_value_at (int         dirfd,
                    const char *name,
                    GError    **error)
{
  g_autoptr(FILE) fp = NULL;
  char line[LINE_MAX], *l;
  int fd;

  fd = bolt_openat (dirfd, name, O_NOFOLLOW | O_CLOEXEC | O_RDONLY, error);

  if (fd < 0)
    return NULL;

  fp = bolt_fdopen (fd, "re", error);
  if (!fp)
    {
      g_prefix_error (error, "could not open %s: ", name);
      return NULL;
    }

  l = fgets (line, sizeof (line) - 1, fp);
  if (!l)
    {
      if (ferror (fp))
        {
          if (errno < 1)
            errno = -EIO;

          g_set_error (error,
                       G_IO_ERROR,
                       g_io_error_from_errno (errno),
                       "io error of file %s: %s",
                       name,
                       g_strerror (errno));
        }

      line[0] = '\0';
    }

  g_strstrip (line);

  return g_strdup (line);
}

gboolean
bolt_write_char_at (int         dirfd,
                    const char *name,
                    char        value,
                    GError    **error)
{
  int fd;
  ssize_t n;

  fd = bolt_openat (dirfd, name, O_WRONLY | O_CLOEXEC, error);
  if (fd < 0)
    return FALSE;

retry:
  n = write (fd, &value, 1);

  if (n == -1)
    {
      int errsv = errno;
      if (errsv == EINTR)
        goto retry;

      /* TODO: handle key error */
      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_errno (errsv),
                   "write error: %s",
                   g_strerror (errno));
    }

  (void) close (fd);

  return n > 0;
}

gboolean
bolt_verify_uid (int         dirfd,
                 const char *want,
                 GError    **error)
{
  g_autoptr(GError) err = NULL;
  g_autofree char *have = NULL;
  gsize want_len;
  gsize have_len;
  gboolean ok;

  have = bolt_read_value_at (dirfd, "unique_id", &err);

  if (have == NULL)
    {
      g_set_error (error, BOLT_ERROR, BOLT_ERROR_FAILED,
                   "unique id verification failed: %s",
                   err->message);
      return FALSE;
    }

  have_len = strlen (have);
  want_len = strlen (want);

  ok = have_len == want_len && !memcmp (want, have, have_len);

  if (!ok)
    g_set_error (error, BOLT_ERROR, BOLT_ERROR_FAILED,
                 "unique id verification failed [%s != %s]",
                 have, want);

  return ok;
}

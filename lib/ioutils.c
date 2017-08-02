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


#include <gio/gio.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "ioutils.h"

// G_DEFINE_AUTOPTR_CLEANUP_FUNC(DIR, closedir);

gboolean
tb_close (int fd, GError **error)
{
  int r;

  r = close (fd);

  if (r == 0)
    return TRUE;

  g_set_error (error, G_IO_ERROR, g_io_error_from_errno (errno), "Could not close file: %s", g_strerror (errno));

  return FALSE;
}

gboolean
tb_write_char (int fd, char data, GError **error)
{
  ssize_t n;

retry:
  n = write (fd, &data, 1);

  if (n == -1)
    {
      int errsv = errno;
      if (errsv == EINTR)
        goto retry;

      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_errno (errsv),
                   "Could not write data: %s",
                   g_strerror (errno));
    }

  return n > 0;
}

int
tb_openat (DIR *d, const char *path, int oflag, GError **error)
{
  int fd = -1;

  fd = openat (dirfd (d), path, oflag);

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

gboolean
tb_verify_uid (int fd, const char *uid, GError **error)
{
  gsize len = strlen (uid);
  char buffer[len];
  ssize_t n;

retry:
  n = read (fd, buffer, sizeof (buffer));
  if (n < 0)
    {
      if (errno == EINTR)
        goto retry;

      g_set_error_literal (error, G_IO_ERROR, g_io_error_from_errno (errno), "Could not read from file");
      return FALSE;
    }
  else if (len != n)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Could not read full uid from file");
      return FALSE;
    }

  if (memcmp (buffer, uid, len))
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "unique id verification failed [%s != %s]",
                   buffer,
                   uid);
      return FALSE;
    }

  return TRUE;
}

ssize_t
tb_read_all (int fd, void *buffer, gsize nbyte, GError **error)
{
  guint8 *ptr   = buffer;
  ssize_t nread = 0;

  do
    {
      ssize_t n = read (fd, ptr, nbyte);

      if (n < 0)
        {
          int errsv = errno;

          if (errsv == EINTR)
            continue;

          g_set_error (error,
                       G_IO_ERROR,
                       g_io_error_from_errno (errsv),
                       "input error while reading: %s",
                       g_strerror (errsv));

          return n > 0 ? n : -errsv;

        }
      else if (n == 0)
        {
          return nread;
        }

      g_assert ((gsize) n <= nbyte);

      ptr += n;
      nread += n;
      nbyte -= n;

    }
  while (nbyte > 0);

  return nread;
}

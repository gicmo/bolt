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
#include <glib/gstdio.h>

#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>

#if !HAVE_FN_COPY_FILE_RANGE
#include <unistd.h>
#include <sys/syscall.h>
#  ifndef __NR_copy_file_range
#    if defined(__x86_64__)
#      define __NR_copy_file_range 326
#    elif defined(__i386__)
#      define __NR_copy_file_range 377
#    else
#      error "__NR_copy_file_range on this architecture"
#    endif
#  endif
#endif

#include "bolt-error.h"
#include "bolt-str.h"

#include "bolt-io.h"

G_DEFINE_AUTOPTR_CLEANUP_FUNC (FILE, fclose);

/* standard open flags for overwriting a file, i.e. close on
 * exec (3), open in write only mode, truncate before writing,
 * and create it if it does not yet exist */
#define BOLT_O_OVERWRITE (O_CLOEXEC | O_WRONLY | O_TRUNC | O_CREAT)

int
bolt_open (const char *path, int flags, int mode, GError **error)
{
  int fd;

  g_return_val_if_fail (error == NULL || *error == NULL, -1);

  fd = g_open (path, flags, mode);

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
  FILE *fp;

  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  fp = fdopen (fd, mode);

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

  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  r = close (fd);

  if (r == 0)
    return TRUE;

  g_set_error (error, G_IO_ERROR,
               g_io_error_from_errno (errno),
               "could not close file: %s",
               g_strerror (errno));

  return FALSE;
}

gboolean
bolt_read_all (int      fd,
               void    *buf,
               gsize    nbytes,
               gsize   *nread,
               GError **error)
{
  char *data = buf;
  gsize count = 0;
  gboolean ok = TRUE;

  g_return_val_if_fail (buf != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  do
    {
      ssize_t n;

      n = read (fd, data, nbytes);

      if (n < 0)
        {
          if (errno == EINTR)
            continue;

          g_set_error (error, G_IO_ERROR,
                       g_io_error_from_errno (errno),
                       "read error: %s",
                       g_strerror (errno));
          ok = FALSE;
          break;
        }
      else if (n == 0)
        {
          break;
        }

      data += n;
      count += n;
      nbytes -= n;

    }
  while (nbytes > 0);

  if (nread)
    *nread = count;

  return ok;
}

gboolean
bolt_write_all (int         fd,
                const void *buf,
                gssize      nbytes,
                GError    **error)
{
  const char *data = buf;
  gboolean ok = TRUE;

  g_return_val_if_fail (buf != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (nbytes < 0)
    nbytes = strlen (data);

  do
    {

      ssize_t n;

      n = write (fd, data, nbytes);
      if (n < 0)
        {
          if (errno == EINTR)
            continue;

          g_set_error (error, G_IO_ERROR,
                       g_io_error_from_errno (errno),
                       "write error: %s",
                       g_strerror (errno));
          ok = FALSE;
        }
      else if (nbytes > 0 && n == 0)
        {
          g_set_error (error, G_IO_ERROR,
                       g_io_error_from_errno (EIO),
                       "write error (zero write)");
          ok = FALSE;
        }

      if (!ok)
        break;

      data += n;
      nbytes -= n;

    }
  while (nbytes > 0);

  return ok;
}

DIR *
bolt_opendir (const char *path,
              GError    **error)
{
  DIR *d = NULL;

  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

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
bolt_openat (int dirfd, const char *path, int oflag, int mode, GError **error)
{
  int fd = -1;

  g_return_val_if_fail (path != NULL, -1);
  g_return_val_if_fail (error == NULL || *error == NULL, -1);

  fd = openat (dirfd, path, oflag, mode);

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

  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  fd = bolt_openat (dirfd, name, oflag, 0, error);
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


gboolean
bolt_closedir (DIR     *d,
               GError **error)
{
  int r;

  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  r = closedir (d);

  if (r < 0)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_errno (errno),
                   "failed close dir: %s",
                   g_strerror (errno));
      return FALSE;
    }

  return TRUE;
}


gboolean
bolt_rmdir (const char *name,
            GError    **error)
{
  int r;

  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  r = rmdir (name);

  if (r < 0)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_errno (errno),
                   "failed to remove directory '%s': %s",
                   name,
                   g_strerror (errno));
      return FALSE;
    }

  return TRUE;
}

gboolean
bolt_unlink (const char *name,
             GError    **error)
{
  int r;

  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  r = unlink (name);

  if (r < 0)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_errno (errno),
                   "failed to unlink '%s': %s",
                   name,
                   g_strerror (errno));
      return FALSE;
    }

  return TRUE;
}


gboolean
bolt_unlink_at (int         dirfd,
                const char *name,
                int         flag,
                GError    **error)
{
  int r;

  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  r = unlinkat (dirfd, name, flag);

  if (r < 0)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   g_io_error_from_errno (errno),
                   "failed to unlink '%s': %s",
                   name,
                   g_strerror (errno));
      return FALSE;
    }

  return TRUE;
}

gboolean
bolt_write_file_at (int         dirfd,
                    const char *name,
                    const char *data,
                    gssize      len,
                    GError    **error)
{
  int fd;
  gboolean ok;

  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  fd = bolt_openat (dirfd, name, BOLT_O_OVERWRITE, 0666, error);

  if (fd < 0)
    return FALSE;

  ok = bolt_write_all (fd, data, len, error);

  if (!ok)
    (void) close (fd);
  else
    ok = bolt_close (fd, error);

  return ok;
}

char *
bolt_read_value_at (int         dirfd,
                    const char *name,
                    GError    **error)
{
  g_autoptr(FILE) fp = NULL;
  char line[LINE_MAX], *l;
  int fd;

  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  fd = bolt_openat (dirfd,
                    name,
                    O_NOFOLLOW | O_CLOEXEC | O_RDONLY,
                    0,
                    error);

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
          return NULL;
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

  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  fd = bolt_openat (dirfd, name, O_WRONLY | O_CLOEXEC, 0, error);
  if (fd < 0)
    return FALSE;

retry:
  n = write (fd, &value, 1);

  if (n == -1)
    {
      int errsv = errno;
      if (errsv == EINTR)
        goto retry;

      if (errsv == ENOKEY)
        {
          g_set_error_literal (error, BOLT_ERROR, BOLT_ERROR_NOKEY,
                               "device does not contain a key");
        }
      else if (errsv == EKEYREJECTED)
        {
          g_set_error_literal (error, BOLT_ERROR, BOLT_ERROR_BADKEY,
                               "key was rejected");
        }
      else
        {
          g_set_error (error,
                       G_IO_ERROR,
                       g_io_error_from_errno (errsv),
                       "write error: %s",
                       g_strerror (errno));
        }
    }

  (void) close (fd);

  return n > 0;
}

gboolean
bolt_read_int_at (int         dirfd,
                  const char *name,
                  gint       *val,
                  GError    **error)
{
  g_autofree char *str = NULL;

  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  str = bolt_read_value_at (dirfd, name, error);

  if (str == NULL)
    return FALSE;

  return bolt_str_parse_as_int (str, val, error);
}

gboolean
bolt_read_uint_at (int         dirfd,
                   const char *name,
                   guint      *val,
                   GError    **error)
{
  g_autofree char *str = NULL;

  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  str = bolt_read_value_at (dirfd, name, error);

  if (str == NULL)
    return FALSE;

  return bolt_str_parse_as_uint (str, val, error);
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

  g_return_val_if_fail (want != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  have = bolt_read_value_at (dirfd, "unique_id", &err);

  if (have == NULL)
    {
      /* make clang's static analyzer happy */
      g_return_val_if_fail (err != NULL, FALSE);
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

gboolean
bolt_file_write_all (const char *fn,
                     const void *data,
                     gssize      n,
                     GError    **error)
{
  g_return_val_if_fail (fn != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  return bolt_write_file_at (AT_FDCWD, fn, data, n, error);
}

gboolean
bolt_ftruncate (int      fd,
                off_t    size,
                GError **error)
{
  int r;

  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  r = ftruncate (fd, size);

  if (r == -1)
    {
      g_set_error (error, G_IO_ERROR,
                   g_io_error_from_errno (errno),
                   "could not truncate file: %s",
                   g_strerror (errno));
      return FALSE;
    }

  return TRUE;
}

int
bolt_mkfifo (const char *path,
             mode_t      mode,
             GError    **error)
{
  int r;

  g_return_val_if_fail (path != NULL, -1);
  g_return_val_if_fail (error == NULL || *error == NULL, -1);

  r = mkfifo (path, mode);

  if (r == -1)
    {
      gint code = g_io_error_from_errno (errno);
      g_set_error (error, G_IO_ERROR, code,
                   "could not create FIFO at '%s': %s",
                   path, g_strerror (errno));
      return -1;
    }

  return r;
}

gboolean
bolt_faddflags (int      fd,
                int      flags,
                GError **error)
{
  int cur;
  int code;

  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  cur = fcntl (fd, F_GETFL);
  if (cur != -1)
    {
      cur |= flags;
      cur = fcntl (fd, F_SETFL, cur);
    }

  if (cur != -1)
    return TRUE;

  code = g_io_error_from_errno (errno);
  g_set_error (error, G_IO_ERROR, code,
               "could not add flags to fd: %s",
               g_strerror (errno));

  return FALSE;
}

gboolean
bolt_fstat (int          fd,
            struct stat *statbuf,
            GError     **error)
{
  int code;
  int r;

  g_return_val_if_fail (statbuf != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  r = fstat (fd, statbuf);

  if (r == 0)
    return TRUE;

  code = errno;
  g_set_error (error, G_IO_ERROR,
               g_io_error_from_errno (code),
               "could not stat file: %s",
               g_strerror (code));

  return FALSE;
}

gboolean
bolt_fstatat (int          dirfd,
              const char  *pathname,
              struct stat *statbuf,
              int          flags,
              GError     **error)
{
  int code;
  int r;

  g_return_val_if_fail (dirfd > -1, FALSE);
  g_return_val_if_fail (pathname != NULL, FALSE);
  g_return_val_if_fail (statbuf != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  r = fstatat (dirfd, pathname, statbuf, flags);

  if (r == 0)
    return TRUE;

  code = errno;
  g_set_error (error, G_IO_ERROR,
               g_io_error_from_errno (code),
               "could not stat file '%s': %s",
               pathname,
               g_strerror (code));

  return FALSE;
}

gboolean
bolt_fdatasync (int      fd,
                GError **error)
{
  int code;
  int r;

  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  r = fdatasync (fd);

  if (r == 0)
    return TRUE;

  code = errno;
  g_set_error (error, G_IO_ERROR,
               g_io_error_from_errno (code),
               "could not sync file data : %s",
               g_strerror (code));

  return FALSE;
}

gboolean
bolt_lseek (int      fd,
            off_t    offset,
            int      whence,
            int     *pos,
            GError **error)
{
  off_t p;

  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  p = lseek (fd, offset, whence);

  if (p == (off_t) -1)
    {
      int code = errno;
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (code),
                   "could not seek file: %s", g_strerror (code));
      return FALSE;
    }

  if (pos)
    *pos = p;

  return TRUE;
}

gboolean
bolt_rename (const char *from,
             const char *to,
             GError    **error)
{
  int code;
  int r;

  g_return_val_if_fail (from != NULL, FALSE);
  g_return_val_if_fail (to != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  r = rename (from, to);

  if (r == 0)
    return TRUE;

  code = errno;
  g_set_error (error, G_IO_ERROR,
               g_io_error_from_errno (code),
               "could not rename '%s' to '%s': %s",
               from, to, g_strerror (code));

  return FALSE;
}

#if !HAVE_FN_COPY_FILE_RANGE
static loff_t
copy_file_range (int          fd_in,
                 loff_t      *off_in,
                 int          fd_out,
                 loff_t      *off_out,
                 size_t       len,
                 unsigned int flags)
{
  return syscall (__NR_copy_file_range,
                  fd_in, off_in,
                  fd_out, off_out,
                  len,
                  flags);
}
#endif

gboolean
bolt_copy_bytes (int      fd_from,
                 int      fd_to,
                 size_t   len,
                 GError **error)
{
  g_return_val_if_fail (fd_from > -1, FALSE);
  g_return_val_if_fail (fd_to > -1, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  do
    {
      ssize_t r;

      r = copy_file_range (fd_from, NULL, fd_to, NULL, len, 0);

      if (r == -1)
        {
          int code = errno;

          g_set_error (error, G_IO_ERROR,
                       g_io_error_from_errno (code),
                       "error while copying data: %s",
                       g_strerror (code));

          return FALSE;
        }
      else if (r == 0)
        {
          break;
        }

      len -= r;

    }
  while (len > 0);

  return len == 0;
}


/* auto cleanup helpers */
void
bolt_cleanup_close_intpr (int *fd)
{
  g_return_if_fail (fd != NULL);

  if (*fd > -1)
    {
      int errsave = errno;
      int r;

      r = close (*fd);

      if (r != 0 && errno == EBADF)
        g_warning ("invalid fd passed to auto cleanup");

      errno = errsave;
    }
}

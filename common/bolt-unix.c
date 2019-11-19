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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Christian J. Kellner <christian@kellner.me>
 */

#include "config.h"

#include "bolt-unix.h"

#include "bolt-error.h"
#include "bolt-io.h"
#include "bolt-names.h"
#include "bolt-str.h"

#include <gio/gio.h>

#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

gboolean
bolt_pid_is_alive (pid_t pid)
{
  gulong p = (gulong) pid;
  char path[256];

  if (pid != 0)
    g_snprintf (path, sizeof (path), "/proc/%lu/stat", p);
  else
    g_snprintf (path, sizeof (path), "/proc/self/stat");

  return g_file_test (path, G_FILE_TEST_EXISTS);
}

gboolean
bolt_sd_notify_literal (const char *state,
                        gboolean   *sent,
                        GError    **error)
{
  bolt_autoclose int fd = -1;
  struct sockaddr_un sau = {AF_UNIX, };
  struct msghdr msghdr = { NULL, };
  struct iovec iovec = { };
  const char *env;
  socklen_t socklen;
  size_t len;
  ssize_t r;

  g_return_val_if_fail (state != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (sent)
    *sent = FALSE;

  env = g_getenv (BOLT_SD_NOTIFY_SOCKET);

  if (env == NULL)
    return TRUE;

  len = strlen (env);

  if (len > sizeof (sau.sun_path) - 1)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "unix domain socket path too long: %s", env);
      return FALSE;
    }

  socklen = offsetof (struct sockaddr_un, sun_path) + len;

  switch (*env)
    {
    case '@':
      /* abstract sockets have sun_path[0] set to '\0',
       * otherwise '\0' have no special meaning, i.e.
       * the address is not null-terminted */
      memcpy (sau.sun_path + 1, env + 1, len);
      socklen += 1; /* the leading '\0' */
      break;

    case '/':
      /* pathname: null-terminated filesystem path */
      memcpy (sau.sun_path, env, len + 1);
      break;

    default:
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "unsupported socket address: %s", env);
      return FALSE;
    }

  fd = socket (AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (fd < 0)
    return bolt_error_for_errno (error, errno, "failed to open socket %m");

  iovec.iov_base = (void *) state;
  iovec.iov_len = strlen (state);

  msghdr.msg_iov = &iovec;
  msghdr.msg_iovlen = 1;
  msghdr.msg_name = &sau;
  msghdr.msg_namelen = socklen;

  r = sendmsg (fd, &msghdr, MSG_NOSIGNAL);

  if (sent)
    *sent = TRUE;

  if (r < 0)
    {
      bolt_error_for_errno (error, errno, "failed to send msg: %m");
      return FALSE;
    }
  else if ((size_t) r != iovec.iov_len)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_MESSAGE_TOO_LARGE,
                   "failed to send complete message: %s", env);
      return FALSE;
    }

  return TRUE;
}

int
bolt_sd_watchdog_enabled (guint64 *timeout,
                          GError **error)
{
  const char *str;
  guint64 val = 0;
  gboolean ok;

  str = g_getenv (BOLT_SD_WATCHDOG_USEC);

  if (str == NULL)
    return 0;

  ok = bolt_str_parse_as_uint64 (str, &val, error);

  if (ok && (val == 0 || val == (guint64) - 1))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "invalid value '%" G_GUINT64_FORMAT "'", val);
      ok = FALSE;
    }

  if (!ok)
    g_prefix_error (error, "failed to parse WATCHDOG_USEC: ");
  else if (timeout)
    *timeout = val;

  return ok ? 1 : -1;
}

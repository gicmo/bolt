/*
 * Copyright © 2018-2019 Red Hat, Inc
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
#include "bolt-io.h"
#include "bolt-macros.h"
#include "bolt-names.h"
#include "bolt-str.h"

#include <glib-unix.h>

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

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

/* Notification Socket */

struct NotifySocket
{
  BoltTmpDir tmpdir;
  char      *socket_path;
  guint      socket_watch;
  int        socket_fd;

  /* */
  guint  counter;
  GQueue messages;
};

union ctrlmsg
{
  struct cmsghdr hdr;
  guint8         buf[CMSG_SPACE (sizeof (struct ucred))];
};


NotifySocket *
notify_socket_new (void)
{
  g_autoptr(GError) err = NULL;
  bolt_autoclose int fd = -1;
  NotifySocket *ns = NULL;
  static const int one = 1;
  struct sockaddr_un sau = {AF_UNIX, {'\0', }};
  size_t socklen;
  int r;

  ns = g_new0 (NotifySocket, 1);

  ns->tmpdir = bolt_tmp_dir_make ("bolt.unix.XXXXXX", &err);
  g_assert_no_error (err);
  g_assert_nonnull (ns->tmpdir);

  fd = socket (AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  assert (fd > -1); /* plain assert for coverity */

  ns->socket_path = g_build_filename (ns->tmpdir, "notify_socket", NULL);

  strncpy (sau.sun_path, ns->socket_path, sizeof (sau.sun_path) - 1);

  socklen =
    offsetof (struct sockaddr_un, sun_path)
    + strlen (sau.sun_path)
    + 1;

  r = bind (fd, &sau, socklen);
  g_assert_cmpint (r, >, -1);

  r = setsockopt (fd, SOL_SOCKET, SO_PASSCRED, &one, sizeof (one));
  g_assert_cmpint (r, >, -1);

  g_queue_init (&ns->messages);
  ns->socket_fd = bolt_steal (&fd, -1);

  g_debug ("notification socket at '%s'", sau.sun_path);
  return ns;
}

void
notify_socket_free (NotifySocket *ns)
{
  g_autoptr(GError) err = NULL;

  g_clear_handle_id (&ns->socket_watch, g_source_remove);

  if (ns->socket_fd > -1)
    {
      bolt_close (ns->socket_fd, &err);
      g_assert_no_error (err);
      ns->socket_fd = -1;
    }

  g_clear_pointer (&ns->tmpdir, bolt_tmp_dir_destroy);
  g_queue_foreach (&ns->messages, (GFunc) g_free, NULL);
  g_queue_clear (&ns->messages);
  g_clear_pointer (&ns->socket_path, g_free);
  g_free (ns);
}

char *
notify_socket_revmsg (NotifySocket *ns, gboolean queue)
{
  char data[4096];
  char *msg;
  struct iovec iov = {
    .iov_base = data,
    .iov_len = sizeof (data) - 1,
  };
  union ctrlmsg crtl = {};
  struct msghdr hdr = {
    .msg_iov = &iov,
    .msg_iovlen = 1,
    .msg_control = &crtl,
    .msg_controllen = sizeof (crtl),
  };
  struct ucred *ucred = NULL;
  ssize_t r;

  /* MSG_TRUNC: return the real size */
  r = recvmsg (ns->socket_fd, &hdr, MSG_DONTWAIT | MSG_CMSG_CLOEXEC | MSG_TRUNC);

  if (r < 0)
    {
      if (errno == EINTR || errno == EAGAIN)
        return NULL;

      g_critical ("i/o error reading from notify socket: %m");
      return NULL;
    }

  if (hdr.msg_flags & MSG_TRUNC || ((size_t) r > sizeof (data) - 1))
    {
      g_warning ("notification message truncated");
      return NULL;
    }

  g_assert_cmpint (r, <, sizeof (data));

  data[r] = '\0';

  ns->counter++;
  msg = g_strdup (data);

  for (struct cmsghdr *c = CMSG_FIRSTHDR (&hdr);
       c != NULL;
       c = CMSG_NXTHDR (&hdr, c))
    {
      if (c->cmsg_level != SOL_SOCKET)
        continue;
      if (c->cmsg_type == SCM_CREDENTIALS &&
          c->cmsg_len == CMSG_LEN (sizeof (struct ucred)))
        ucred = (struct ucred *) (void *) CMSG_DATA (c);
    }

  if (queue)
    g_queue_push_tail (&ns->messages, msg);

  g_debug ("got message: '%s' [%s]", msg, bolt_yesno (queue));
  if (ucred != NULL)
    g_debug ("  ucred, pid: %i, uid: %li, gid: %li",
             (int) ucred->pid, (long) ucred->uid, (long) ucred->gid);

  return msg;
}

static gboolean
got_notification (gpointer user_data)
{
  NotifySocket *ns = (NotifySocket *) user_data;

  notify_socket_revmsg (ns, TRUE);

  return TRUE;
}

void
notify_socket_enable_watch (NotifySocket *ns)
{
  g_autoptr(GSource) source = NULL;

  g_assert_nonnull (ns);
  g_assert_cmpuint (ns->socket_fd, >, -1);

  source = g_unix_fd_source_new (ns->socket_fd, G_IO_IN);
  g_assert_nonnull (source);

  g_source_set_callback (source, got_notification, ns, NULL);
  ns->socket_watch = g_source_attach (source, NULL);
}

void
notify_socket_set_environment (NotifySocket *ns)
{
  g_setenv (BOLT_SD_NOTIFY_SOCKET, ns->socket_path, TRUE);
}

void
notify_socket_make_pollfd (NotifySocket *ns,
                           GPollFD      *fd)
{
  memset (fd, 0, sizeof (GPollFD));
  fd->fd = ns->socket_fd;
  fd->events =  G_IO_IN | G_IO_HUP | G_IO_ERR;
}

/* Version parsing, checking */
static gboolean
parse_one (const char  *str,
           BoltVersion *version,
           int         *index,
           GError     **error)
{
  gboolean ok;
  gint64 v;
  int i = *index;

  ok = g_ascii_string_to_signed (str,
                                 10, /* base */
                                 0, G_MAXINT,
                                 &v,
                                 error);

  if (!ok)
    return FALSE;

  version->triplet[i] = (int) v;
  *index = i + 1;

  return TRUE;
}

gboolean
bolt_version_parse (const char  *str,
                    BoltVersion *version,
                    GError     **error)
{
  g_autofree char *tmp = NULL;
  gboolean ok = FALSE;
  char *data = NULL;
  char *delim = NULL;
  int index = 0;

  g_return_val_if_fail (str != NULL, FALSE);
  g_return_val_if_fail (version != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  bolt_version_clear (version);

  /* so we manipulate the string */
  tmp = data = g_strdup (str);
  (void) tmp; /* 'dead' store for g_autofree */

  delim = strchr (data, '-');

  if (delim)
    {
      *delim = '\0';
      version->suffix = g_strdup (delim + 1);
    }

  while (index < 2 && (delim = strchr (data, '.')) != NULL)
    {
      *delim = '\0';
      ok = parse_one (data, version, &index, error);

      if (!ok)
        return FALSE;

      data = delim + 1;
    }

  return parse_one (data, version, &index, error);
}

void
bolt_version_clear (BoltVersion *version)
{
  version->major = -1;
  version->minor = -1;
  version->patch = -1;

  g_clear_pointer (&version->suffix, g_free);
}

/* strcmp semantics */
int
bolt_version_compare (BoltVersion *a,
                      BoltVersion *b)
{
  g_return_val_if_fail (a != NULL, -1);
  g_return_val_if_fail (b != NULL,  1);

  for (int i = 0; i < 3; i++)
    {
      int ac = a->triplet[i];
      int bc = b->triplet[i];

      if (ac < bc)
        return -1;
      else if (ac > bc)
        return 1;
    }

  return 0; /* all components equal */
}

gboolean
bolt_version_check (BoltVersion *base,
                    int          major,
                    int          minor,
                    int          patch)
{
  BoltVersion ref = BOLT_VERSION_INIT (major, minor, patch);

  g_return_val_if_fail (base != NULL, FALSE);

  return bolt_version_compare (base, &ref) >= 0;
}

gboolean
bolt_check_kernel_version (int major, int minor)
{
  g_autoptr(GError) err = NULL;
  g_auto(BoltVersion) ver = BOLT_VERSION_INIT (1, 0, 0);
  g_autofree char *data = NULL;
  gboolean ok;
  gsize length;

  ok = g_file_get_contents ("/proc/sys/kernel/osrelease",
                            &data,
                            &length,
                            &err);

  if (!ok)
    {
      g_message ("Could not read kernel version: %s", err->message);
      return FALSE;
    }

  while (length > 1 && data[length - 1] == '\n')
    data[--length] = '\0';

  ok = bolt_version_parse (data, &ver, &err);
  if (!ok)
    {
      g_message ("Could not parse kernel version (%s): %s",
                 data, err->message);
      return FALSE;
    }

  g_debug ("Read kernel version: %d.%d.%d (%s)",
           ver.major, ver.minor, ver.patch,
           (ver.suffix ? : ""));

  return bolt_version_check (&ver, major, minor, -1);
}

typedef struct MainLoopCtx
{
  GMainLoop *loop;
  gboolean   timeout;
} MainLoopCtx;

static gboolean
on_main_loop_timeout (gpointer user_data)
{
  MainLoopCtx *ctx = user_data;

  ctx->timeout = TRUE;
  g_main_loop_quit (ctx->loop);

  return G_SOURCE_REMOVE;
}

gboolean
bolt_test_run_main_loop (GMainLoop *loop,
                         guint      timeout_seconds,
                         gboolean   exit_on_timeout,
                         GError   **error)
{
  MainLoopCtx ctx = { .loop = loop, .timeout = FALSE };
  guint tid;

  tid = g_timeout_add_seconds (timeout_seconds,
                               on_main_loop_timeout,
                               &ctx);

  if (ctx.timeout)
    {
      const char *message = "Operation timed out";
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
                   "%s", message);

      if (exit_on_timeout)
        {
          g_warning ("test error: %s", message);
          g_assert_not_reached ();
        }

      return FALSE;
    }

  g_source_remove (tid);

  return TRUE;
}

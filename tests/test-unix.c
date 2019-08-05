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

#include "bolt-unix.h"

#include "bolt-io.h"
#include "bolt-macros.h"
#include "bolt-names.h"
#include "bolt-str.h"
#include "bolt-test.h"

#include <glib-unix.h>
#include <gio/gio.h>

#include <errno.h>
#include <locale.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h> /* unlinkat, fork */

typedef struct TestDummy
{
  int dummy;
} TestDummy;

static void
test_pid_is_alive (TestDummy *tt, gconstpointer user_data)
{
  gboolean ok;
  pid_t p;
  pid_t r;
  int status;

  ok = bolt_pid_is_alive (0);
  g_assert_true (ok);

  p = fork ();
  g_assert_cmpint ((int) p, >, -1);

  if (p == 0)
    /* child */
    exit (42);
  /* parent */
  ok = bolt_pid_is_alive (p);
  g_assert_true (ok);

  r = waitpid (0, &status, 0);
  g_assert_cmpint ((int) r, ==, (int) p);

  ok = bolt_pid_is_alive (p);
  g_assert_false (ok);
}

typedef struct TestNotify
{
  BoltTmpDir tmpdir;
  char      *socket_path;
  guint      socket_watch;
  int        socket_fd;

  /* */
  guint  counter;
  GQueue messages;
} TestNotify;

union ctrlmsg
{
  struct cmsghdr hdr;
  guint8         buf[CMSG_SPACE (sizeof (struct ucred))];
};

static char *
test_notify_revmsg (TestNotify *tt, gboolean queue)
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
  r = recvmsg (tt->socket_fd, &hdr, MSG_DONTWAIT | MSG_CMSG_CLOEXEC | MSG_TRUNC);

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

  tt->counter++;
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
    g_queue_push_tail (&tt->messages, msg);

  g_debug ("got message: '%s' [%s]", msg, bolt_yesno (queue));
  if (ucred != NULL)
    g_debug ("  ucred, pid: %i, uid: %li, gid: %li",
             (int) ucred->pid, (long) ucred->uid, (long) ucred->gid);

  return msg;
}

static gboolean
got_notification (gpointer user_data)
{
  TestNotify *tt = (TestNotify *) user_data;

  test_notify_revmsg (tt, TRUE);

  return TRUE;
}

static void
test_notify_setup (TestNotify *tt, gconstpointer data)
{
  g_autoptr(GError) err = NULL;
  bolt_autoclose int fd = -1;
  static const int one = 1;
  struct sockaddr_un sau = {AF_UNIX, {'\0', }};
  size_t socklen;
  int r;

  tt->tmpdir = bolt_tmp_dir_make ("bolt.unix.XXXXXX", &err);
  g_assert_no_error (err);
  g_assert_nonnull (tt->tmpdir);

  fd = socket (AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  g_assert_cmpint (fd, >, -1);

  tt->socket_path = g_build_filename (tt->tmpdir, "notify_socket", NULL);

  strncpy (sau.sun_path, tt->socket_path, sizeof (sau.sun_path) - 1);

  socklen =
    offsetof (struct sockaddr_un, sun_path)
    + strlen (sau.sun_path)
    + 1;

  r = bind (fd, &sau, socklen);
  g_assert_cmpint (r, >, -1);

  r = setsockopt (fd, SOL_SOCKET, SO_PASSCRED, &one, sizeof (one));
  g_assert_cmpint (r, >, -1);

  g_queue_init (&tt->messages);
  tt->socket_fd = bolt_steal (&fd, -1);

  g_debug ("notification socket at '%s'", sau.sun_path);
}

static void
test_notify_teardown (TestNotify *tt, gconstpointer user)
{
  g_autoptr(GError) err = NULL;

  g_clear_handle_id (&tt->socket_watch, g_source_remove);

  if (tt->socket_fd > -1)
    {
      bolt_close (tt->socket_fd, &err);
      g_assert_no_error (err);
      tt->socket_fd = -1;
    }

  g_clear_pointer (&tt->tmpdir, bolt_tmp_dir_destroy);
  g_queue_free_full (&tt->messages, g_free);
}

static void test_notify_enable_watch (TestNotify *tt) G_GNUC_UNUSED;

static void
test_notify_enable_watch (TestNotify *tt)
{
  g_autoptr(GSource) source = NULL;

  g_assert_nonnull (tt);
  g_assert_cmpuint (tt->socket_fd, >, -1);

  source = g_unix_fd_source_new (tt->socket_fd, G_IO_IN);
  g_assert_nonnull (source);

  g_source_set_callback (source, got_notification, tt, NULL);
  tt->socket_watch = g_source_attach (source, NULL);
}

static void
test_notify_set_environment (TestNotify *tt)
{
  g_setenv ("NOTIFY_SOCKET", tt->socket_path, TRUE);
}

static void
test_sd_notify (TestNotify *tt, gconstpointer user)
{
  g_autoptr(GError) err = NULL;
  g_autofree char *verylong = NULL;
  const char *ref = NULL;
  gboolean sent;
  gboolean ok;
  size_t l;
  char *msg;

  /* no socket at all */
  ref = "STATUS=this is my message";

  ok = bolt_sd_notify_literal (ref, &sent, &err);
  g_assert_no_error (err);
  g_assert_true (ok);
  g_assert_false (sent);

  /* invalid/unsupported destinations */
  g_setenv ("NOTIFY_SOCKET", "INVALID SOCKET", TRUE);
  ok = bolt_sd_notify_literal (ref, &sent, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
  g_assert_false (ok);
  g_assert_false (sent);
  g_clear_error (&err);

  /* socket dest too long */
  l = sizeof (((struct sockaddr_un *) 0)->sun_path) + 10;
  g_assert_cmpuint (l, <, 1024);
  verylong = g_strnfill (l, 'a');

  g_setenv ("NOTIFY_SOCKET", verylong, TRUE);
  ok = bolt_sd_notify_literal (ref, &sent, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  g_assert_false (ok);
  g_assert_false (sent);
  g_clear_error (&err);

  /* peer does not exist */
  g_setenv ("NOTIFY_SOCKET", "@NONEXISTANTABSTRACT", TRUE);
  ok = bolt_sd_notify_literal (ref, &sent, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_CONNECTION_REFUSED);
  g_assert_false (ok);
  g_assert_true (sent);
  g_clear_error (&err);

  /* finally the VALID socket */
  test_notify_set_environment (tt);

  ok = bolt_sd_notify_literal (ref, &sent, &err);
  g_assert_no_error (err);
  g_assert_true (ok);
  g_assert_true (sent);

  msg = test_notify_revmsg (tt, FALSE);
  g_assert_nonnull (msg);
  g_assert_cmpstr (msg, ==, ref);
  g_free (msg);
}

static void
test_sd_watchdog_enabled (TestDummy *tt, gconstpointer user_data)
{
  g_autoptr(GError) err = NULL;
  g_autofree char *tmp = NULL;
  guint64 val;
  int r;

  /* no env variable */
  g_assert_null (g_getenv (BOLT_SD_WATCHDOG_USEC));

  r = bolt_sd_watchdog_enabled (&val, &err);
  g_assert_no_error (err);
  g_assert_cmpint (r, ==, 0);

  /* empty env variable [error] */
  g_setenv (BOLT_SD_WATCHDOG_USEC, "", TRUE);
  r = bolt_sd_watchdog_enabled (&val, &err);
  g_assert_nonnull (err);
  g_assert_cmpint (r, <, 0);
  g_clear_error (&err);

  /* invalid env variable [error] */
  g_setenv (BOLT_SD_WATCHDOG_USEC, "NOT-A-NUMBER", TRUE);
  r = bolt_sd_watchdog_enabled (&val, &err);
  g_assert_nonnull (err);
  g_assert_cmpint (r, <, 0);
  g_clear_error (&err);

  /* valid number, finally */
  tmp = g_strdup_printf ("%d", 42 * G_USEC_PER_SEC);
  g_setenv (BOLT_SD_WATCHDOG_USEC, tmp, TRUE);
  r = bolt_sd_watchdog_enabled (&val, &err);
  g_assert_no_error (err);
  g_assert_cmpint (r, >, 0);
}

int
main (int argc, char **argv)
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  g_test_add ("/common/unix/pid_is_alive",
              TestDummy,
              NULL,
              NULL,
              test_pid_is_alive,
              NULL);

  g_test_add ("/common/unix/bolt_sd_notify",
              TestNotify,
              NULL,
              test_notify_setup,
              test_sd_notify,
              test_notify_teardown);

  g_test_add ("/common/unix/sd_watchdog_enabled",
              TestDummy,
              NULL,
              NULL,
              test_sd_watchdog_enabled,
              NULL);

  return g_test_run ();
}

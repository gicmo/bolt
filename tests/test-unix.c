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

#include <gio/gio.h>

#include <locale.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h> /* waitpid */
#include <unistd.h>   /* fork */

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
  NotifySocket *ns;
} TestNotify;

static void
test_notify_setup (TestNotify *tt, gconstpointer data)
{
  tt->ns = notify_socket_new ();
}

static void
test_notify_teardown (TestNotify *tt, gconstpointer user)
{
  g_clear_pointer (&tt->ns, notify_socket_free);
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
  g_setenv (BOLT_SD_NOTIFY_SOCKET, "INVALID SOCKET", TRUE);
  ok = bolt_sd_notify_literal (ref, &sent, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
  g_assert_false (ok);
  g_assert_false (sent);
  g_clear_error (&err);

  /* socket dest too long */
  l = sizeof (((struct sockaddr_un *) 0)->sun_path) + 10;
  g_assert_cmpuint (l, <, 1024);
  verylong = g_strnfill (l, 'a');

  g_setenv (BOLT_SD_NOTIFY_SOCKET, verylong, TRUE);
  ok = bolt_sd_notify_literal (ref, &sent, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  g_assert_false (ok);
  g_assert_false (sent);
  g_clear_error (&err);

  /* peer does not exist */
  g_setenv (BOLT_SD_NOTIFY_SOCKET, "@NONEXISTANTABSTRACT", TRUE);
  ok = bolt_sd_notify_literal (ref, &sent, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_CONNECTION_REFUSED);
  g_assert_false (ok);
  g_assert_true (sent);
  g_clear_error (&err);

  /* finally the VALID socket */
  notify_socket_set_environment (tt->ns);

  ok = bolt_sd_notify_literal (ref, &sent, &err);
  g_assert_no_error (err);
  g_assert_true (ok);
  g_assert_true (sent);

  msg = notify_socket_revmsg (tt->ns, FALSE);
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

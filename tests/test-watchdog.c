/*
 * Copyright © 2019 Red Hat, Inc
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

#include "bolt-watchdog.h"

#include "bolt-names.h"
#include "bolt-test.h"
#include "bolt-time.h"

#include <locale.h>

typedef struct TestWatchdog
{
  NotifySocket *ns;

  /* */
  guint64 timeout; /* in usec */
  char   *timestr;

  /* */
  GArray *pulses;

  /* */
  GCancellable *cancel;
} TestWatchdog;

static void
test_notify_setup (TestWatchdog *tt, gconstpointer data)
{
  tt->ns = notify_socket_new ();
  g_assert_nonnull (tt->ns);

  tt->timeout = 6 * G_USEC_PER_SEC;
  tt->timestr = g_strdup_printf ("%" G_GUINT64_FORMAT, tt->timeout);

  tt->pulses = g_array_new (FALSE, FALSE, sizeof (guint64));
  tt->cancel = g_cancellable_new ();
}

static void
test_notify_teardown (TestWatchdog *tt, gconstpointer user)
{
  g_clear_pointer (&tt->ns, notify_socket_free);
  g_clear_pointer (&tt->timestr, g_free);
  g_clear_pointer (&tt->pulses, g_array_unref);
  g_clear_object (&tt->cancel);
}


static void
test_watchdog_basic (TestWatchdog *tt, gconstpointer user_data)
{
  g_autoptr(GError) err = NULL;
  g_autoptr(BoltWatchdog) dog = NULL;
  guint64 timeout = 0;
  guint pulse;

  /* watchdog env not set */
  dog = bolt_watchdog_new (&err);
  g_assert_no_error (err);
  g_assert_nonnull (dog);

  g_object_get (dog,
                "timeout", &timeout,
                "pulse", &pulse,
                NULL);

  g_assert_cmpuint (timeout, ==, 0);
  g_assert_cmpuint (pulse, ==, 0);

  /* invalid watchdog env */
  g_setenv (BOLT_SD_WATCHDOG_USEC, "INVALID", TRUE);

  g_clear_object (&dog);

  dog = bolt_watchdog_new (&err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  g_assert_null (dog);
  g_clear_error (&err);
  g_clear_object (&dog);

  /* now with some actual valid socket, watchdog */
  notify_socket_set_environment (tt->ns);
  g_setenv (BOLT_SD_WATCHDOG_USEC, tt->timestr, TRUE);

  g_clear_object (&dog);
  dog = bolt_watchdog_new (&err);
  g_assert_no_error (err);
  g_assert_nonnull (dog);

  g_object_get (dog, "timeout", &timeout, NULL);
  g_assert_cmpuint (timeout, ==, tt->timeout);

  g_object_get (dog, "pulse", &pulse, NULL);
  g_assert_cmpuint (pulse, ==, timeout / 2 / G_USEC_PER_SEC);
}

static gpointer
test_watchdog_receiver (gpointer data)
{
  TestWatchdog *tt = data;
  GPollFD fds[2];
  gboolean ok;

  notify_socket_make_pollfd (tt->ns, &fds[0]);

  ok = g_cancellable_make_pollfd (tt->cancel, &fds[1]);
  g_assert_true (ok);

  while (!g_cancellable_is_cancelled (tt->cancel))
    {
      const char *msg;
      guint64 now;
      gint r;

      r = g_poll (fds, G_N_ELEMENTS (fds), -1);
      g_assert_cmpint (r, >, -1);

      if (!fds[0].revents)
        continue;

      msg = notify_socket_revmsg (tt->ns, TRUE);

      if (!g_str_has_prefix (msg, "WATCHDOG"))
        continue;

      now = bolt_now_in_seconds ();
      g_array_append_val (tt->pulses, now);

      g_debug ("%" G_GUINT64_FORMAT ": pulse received", now);

      fds[0].revents = fds[1].revents = 0;
    }

  return NULL;
}

static gboolean
on_timeout_warn_quit_loop (gpointer user_data)
{
  GMainLoop *loop = user_data;

  g_main_loop_quit (loop);

  return G_SOURCE_REMOVE;
}

static void
test_watchdog_timeout (TestWatchdog *tt, gconstpointer user_data)
{
  g_autoptr(GError) err = NULL;
  g_autoptr(BoltWatchdog) dog = NULL;
  g_autoptr(GMainLoop) loop = NULL;
  GThread *thread;
  guint64 timeout = 0;
  guint pulse;
  guint n = 10;

  skip_test_unless (g_test_slow (), "slow tests disabled");

  notify_socket_set_environment (tt->ns);
  g_setenv (BOLT_SD_WATCHDOG_USEC, tt->timestr, TRUE);

  dog = bolt_watchdog_new (&err);
  g_assert_no_error (err);
  g_assert_nonnull (dog);

  g_object_get (dog, "timeout", &timeout, NULL);
  g_assert_cmpuint (timeout, ==, tt->timeout);

  g_object_get (dog, "pulse", &pulse, NULL);
  g_assert_cmpuint (pulse, ==, timeout / 2 / G_USEC_PER_SEC);

  thread = g_thread_new ("NotifySocket", test_watchdog_receiver, tt);

  loop = g_main_loop_new (NULL, FALSE);

  /* we wait for 11 pulses */
  g_timeout_add_seconds (pulse * n + 1, on_timeout_warn_quit_loop, loop);

  /* now we wait */
  g_main_loop_run (loop);

  /* stop the background thread */
  g_cancellable_cancel (tt->cancel);

  /* we got here because the timeout kicked in */
  g_assert_cmpuint (tt->pulses->len, >=, n);

  for (guint i = 1; i < n; i++)
    {
      guint64 before = g_array_index (tt->pulses, guint64, i - 1);
      guint64 after = g_array_index (tt->pulses, guint64, i);
      gint64 diff = after - before;

      g_debug ("%u - %u: %" G_GINT64_FORMAT "s", i, i - 1, diff);
      g_assert_cmpint (diff, <, tt->timeout / G_USEC_PER_SEC);

    }

  (void) g_thread_join (thread);
}


int
main (int argc, char **argv)
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  g_test_add ("/boltd/watchdog/basic",
              TestWatchdog,
              NULL,
              test_notify_setup,
              test_watchdog_basic,
              test_notify_teardown);

  g_test_add ("/boltd/watchdog/timeout",
              TestWatchdog,
              NULL,
              test_notify_setup,
              test_watchdog_timeout,
              test_notify_teardown);

  return g_test_run ();
}

/*
 * Copyright © 2020 Red Hat, Inc
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

#include "bolt-reaper.h"

#include "bolt-dbus.h"
#include "bolt-unix.h"

#include <locale.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>


typedef struct
{
  int dummy;
} TestReaper;

static gboolean
warn_quit_loop (gpointer user_data)
{
  GMainLoop *loop = user_data;

  g_warning ("timeout reached");
  g_main_loop_quit (loop);

  return G_SOURCE_REMOVE;
}

static void
process_died (GObject    *gobject,
              guint       pid,
              const char *name,
              gpointer    user_data)
{

  GMainLoop *loop = user_data;

  g_debug ("%u (%s) died", pid, name);

  if (g_main_loop_is_running (loop))
    g_main_loop_quit (loop);
}

/*  */

static void
test_reaper_object (TestReaper *tt, gconstpointer user)
{
  g_autoptr(BoltReaper) reaper = NULL;
  guint timeout;
  gboolean found;

  reaper = g_object_new (BOLT_TYPE_REAPER, NULL);

  g_object_get (reaper, "timeout", &timeout, NULL);
  g_assert_cmpuint (timeout, >, 0);

  g_clear_object (&reaper);

  reaper = g_object_new (BOLT_TYPE_REAPER, "timeout", 10, NULL);
  g_object_get (reaper, "timeout", &timeout, NULL);
  g_assert_cmpuint (timeout, ==, 10);

  bolt_reaper_add_pid (reaper, 23, NULL);

  found = bolt_reaper_has_pid (reaper, 23);
  g_assert_true (found);

  found = bolt_reaper_del_pid (reaper, 23);
  g_assert_true (found);

  found = bolt_reaper_del_pid (reaper, 23);
  g_assert_false (found);
}

static void
test_reaper_basic (TestReaper *tt, gconstpointer user)
{
  g_autoptr(BoltReaper) reaper = NULL;
  g_autoptr(GMainLoop) loop = NULL;
  guint tid;
  pid_t pid;
  int r;

  pid = fork ();
  g_assert_cmpint (pid, !=, -1);

  if (pid == 0)
    /* child, do nothing but exit */
    exit (0);

  g_assert_true (bolt_pid_is_alive (pid));

  pid = waitpid (pid, &r, 0);
  g_assert_cmpint (pid, >, 0);
  g_assert_cmpint (r, ==, 0);

  loop = g_main_loop_new (NULL, FALSE);
  reaper = g_object_new (BOLT_TYPE_REAPER,
                         "timeout", 500,
                         NULL);
  g_assert_nonnull (reaper);

  bolt_reaper_add_pid (reaper, (pid_t) pid, "foo");

  g_signal_connect (reaper, "process-died",
                    G_CALLBACK (process_died),
                    loop);

  tid = g_timeout_add_seconds (5, warn_quit_loop, loop);
  g_main_loop_run (loop);
  g_clear_handle_id (&tid, g_source_remove);

}

int
main (int argc, char **argv)
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  bolt_dbus_ensure_resources ();

  g_test_add ("/reaper/object",
              TestReaper,
              NULL,
              NULL,
              test_reaper_object,
              NULL);

  g_test_add ("/reaper/basic",
              TestReaper,
              NULL,
              NULL,
              test_reaper_basic,
              NULL);

  return g_test_run ();
}

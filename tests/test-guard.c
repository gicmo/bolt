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

#include "bolt-guard.h"

#include "bolt-dbus.h"
#include "bolt-fs.h"
#include "bolt-test.h"

#include <glib.h>
#include <gio/gio.h>
#include <glib/gprintf.h>
#include <glib/gstdio.h>

#include <assert.h>
#include <locale.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>

typedef struct
{
  char *rundir;
} TestGuard;

static void
test_guard_setup (TestGuard *tt, gconstpointer data)
{
  g_autoptr(GError) err = NULL;

  tt->rundir = g_strdup (g_getenv ("BOLT_RUNDIR"));
  if (tt->rundir == NULL)
    tt->rundir = g_dir_make_tmp ("bolt.guard.XXXXXX", &err);

  g_assert_no_error (err);
  g_assert_nonnull (tt->rundir);

  g_debug ("rundir at '%s'", tt->rundir);
}

static void
test_guard_tear_down (TestGuard *tt, gconstpointer user)
{
  g_autoptr(GError) err = NULL;
  gboolean ok;

  ok = bolt_fs_cleanup_dir (tt->rundir, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  g_clear_pointer (&tt->rundir, g_free);
}

static void
on_release_true (BoltGuard *guard, gboolean *released)
{
  g_assert_cmpstr (bolt_guard_get_id (guard), ==, "guard-1");
  g_assert_cmpstr (bolt_guard_get_who (guard), ==, "Richard III");
  g_assert_cmpuint (bolt_guard_get_pid (guard), ==, getpid ());

  *released = TRUE;
}

static void
test_guard_basic (TestGuard *tt, gconstpointer user)
{
  g_autoptr(BoltGuard) guard = NULL;
  g_autoptr(GError) err = NULL;
  g_autoptr(GFile) f = NULL;
  const char *id = "guard-1";
  const char *who = "Richard III";
  gboolean released = FALSE;
  gboolean ok;
  pid_t pid = getpid ();

  guard = g_object_new (BOLT_TYPE_GUARD,
                        "id", id,
                        "who", who,
                        "pid", pid,
                        NULL);

  g_assert_nonnull (guard);

  g_assert_cmpstr (id, ==, bolt_guard_get_id (guard));
  g_assert_cmpstr (who, ==, bolt_guard_get_who (guard));
  g_assert_cmpuint ((guint) pid, ==, bolt_guard_get_pid (guard));
  g_assert_null (bolt_guard_get_path (guard));
  g_assert_null (bolt_guard_get_fifo (guard));

  f = g_file_new_for_path (tt->rundir);

  ok = bolt_guard_save (guard, f, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  g_assert_nonnull (bolt_guard_get_path (guard));

  g_signal_connect (guard, "released",
                    (GCallback) on_release_true,
                    &released);

  g_assert_false (released);
  /* release the guard */
  g_clear_object (&guard);
  g_assert_true (released);
}

static gboolean
on_cb_close_fd (gpointer user_data)
{
  int *fd = user_data;
  int r;

  g_debug ("closing fd");
  r = close (*fd);

  g_assert_cmpint (r, >, -1);
  *fd = -1;
  return FALSE;
}

static void
quit_mainloop (BoltGuard *guard, GMainLoop *loop)
{
  g_debug ("Stopping loop");
  g_main_loop_quit (loop);
}

static void
test_guard_recover_active (TestGuard *tt, gconstpointer user)
{
  g_autoptr(GPtrArray) guards = NULL;
  g_autoptr(BoltGuard) guard = NULL;
  g_autoptr(GMainLoop) loop = NULL;
  g_autoptr(GError) err = NULL;
  g_autoptr(GFile) f = NULL;
  g_autofree char *fifo = NULL;
  const char *id = "guard-1";
  const char *who = "Richard III";
  BoltGuard *g = NULL;
  gboolean released = FALSE;
  gboolean ok;
  gpointer data;
  int fd;

  guard = g_object_new (BOLT_TYPE_GUARD,
                        "id", id,
                        "who", who,
                        "pid", getpid (),
                        NULL);

  g_assert_nonnull (guard);

  f = g_file_new_for_path (tt->rundir);

  ok = bolt_guard_save (guard, f, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  g_assert_nonnull (bolt_guard_get_path (guard));

  g_signal_connect (guard, "released",
                    (GCallback) on_release_true,
                    &released);

  g_assert_false (released);

  fd = bolt_guard_monitor (guard, &err);
  g_assert_no_error (err);
  g_assert_cmpint (fd, >, -1);
  g_object_unref (guard); /* monitor adds a references */

  /* memorize the fifo so we can check it exists
   * after we release the guard */
  fifo = g_strdup (bolt_guard_get_fifo (guard));
  g_assert_nonnull (fifo);
  ok = g_file_test (fifo, G_FILE_TEST_EXISTS);
  g_assert_true (ok);

  /* release the kraken */
  g_clear_object (&guard);
  g_assert_true (released);

  ok = g_file_test (fifo, G_FILE_TEST_EXISTS);
  g_assert_true (ok);

  /* recover the guard */
  guards = bolt_guard_recover (tt->rundir, &err);
  g_assert_no_error (err);
  g_assert_cmpuint (guards->len, ==, 1);
  g = g_ptr_array_index (guards, 0);

  g_assert_cmpstr (bolt_guard_get_id (g), ==, id);
  g_assert_cmpstr (bolt_guard_get_who (g), ==, who);
  g_assert_cmpuint (bolt_guard_get_pid (g), ==, getpid ());

  released = FALSE;
  g_signal_connect (g, "released",
                    (GCallback) on_release_true,
                    &released);

  loop = g_main_loop_new (NULL, FALSE);

  g_signal_connect (g, "released",
                    (GCallback) quit_mainloop,
                    loop);

  /* schedule a closing of the fifo */
  g_idle_add (on_cb_close_fd, (gpointer) & fd);

  /* fail if we don't have anything after n seconds */
  bolt_test_run_main_loop (loop, 5, TRUE, NULL);

  /* free the array without calling its destroy function */
  data = g_ptr_array_free (guards, FALSE);
  g_free (data);
  guards = NULL;
}

static void
test_guard_recover_dead (TestGuard *tt, gconstpointer user)
{
  g_autoptr(GPtrArray) guards = NULL;
  g_autoptr(BoltGuard) guard = NULL;
  g_autoptr(GMainLoop) loop = NULL;
  g_autoptr(GError) err = NULL;
  g_autoptr(GFile) f = NULL;
  g_autofree char *fifo = NULL;
  const char *id = "guard-1";
  const char *who = "Richard III";
  BoltGuard *g = NULL;
  gboolean released = FALSE;
  gboolean ok;
  gpointer data;
  int fd;

  guard = g_object_new (BOLT_TYPE_GUARD,
                        "id", id,
                        "who", who,
                        "pid", getpid (),
                        NULL);

  g_assert_nonnull (guard);

  f = g_file_new_for_path (tt->rundir);

  ok = bolt_guard_save (guard, f, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  g_assert_nonnull (bolt_guard_get_path (guard));

  g_signal_connect (guard, "released",
                    (GCallback) on_release_true,
                    &released);

  g_assert_false (released);

  fd = bolt_guard_monitor (guard, &err);
  g_assert_no_error (err);
  assert (fd > -1); /* plain assert for coverity */
  g_object_unref (guard); /* monitor adds a references */

  /* memorize the fifo so we can check it exists
   * after we release the guard */
  fifo = g_strdup (bolt_guard_get_fifo (guard));
  g_assert_nonnull (fifo);
  ok = g_file_test (fifo, G_FILE_TEST_EXISTS);
  g_assert_true (ok);

  /* release the kraken */
  g_clear_object (&guard);
  g_assert_true (released);

  ok = g_file_test (fifo, G_FILE_TEST_EXISTS);
  g_assert_true (ok);

  /* simulate that the client meanwhile closed the FIFO */
  (void) close (fd);

  /* recover the guard */
  guards = bolt_guard_recover (tt->rundir, &err);
  g_assert_no_error (err);
  g_assert_cmpuint (guards->len, ==, 1);
  g = g_ptr_array_index (guards, 0);

  g_assert_cmpstr (bolt_guard_get_id (g), ==, id);
  g_assert_cmpstr (bolt_guard_get_who (g), ==, who);
  g_assert_cmpuint (bolt_guard_get_pid (g), ==, getpid ());

  released = FALSE;
  g_signal_connect (g, "released",
                    (GCallback) on_release_true,
                    &released);

  loop = g_main_loop_new (NULL, FALSE);

  g_signal_connect (g, "released",
                    (GCallback) quit_mainloop,
                    loop);

  /* fail if we don't have anything after n seconds */
  bolt_test_run_main_loop (loop, 5, TRUE, NULL);

  /* free the array without calling its destroy function */
  data = g_ptr_array_free (guards, FALSE);
  g_free (data);
  guards = NULL;
}

int
main (int argc, char **argv)
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  bolt_dbus_ensure_resources ();

  g_test_add ("/guard/basic",
              TestGuard,
              NULL,
              test_guard_setup,
              test_guard_basic,
              test_guard_tear_down);

  g_test_add ("/guard/recover/active",
              TestGuard,
              NULL,
              test_guard_setup,
              test_guard_recover_active,
              test_guard_tear_down);

  g_test_add ("/guard/recover/dead",
              TestGuard,
              NULL,
              test_guard_setup,
              test_guard_recover_dead,
              test_guard_tear_down);

  return g_test_run ();
}

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

#include "bolt-power.h"

#include "bolt-dbus.h"
#include "bolt-fs.h"
#include "bolt-macros.h"
#include "bolt-str.h"
#include "mock-sysfs.h"

#include <glib.h>
#include <gio/gio.h>
#include <glib/gprintf.h>
#include <glib/gstdio.h>

#include <libudev.h>
#include <locale.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>

typedef struct udev_device udev_device;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (udev_device, udev_device_unref);


typedef struct
{
  MockSysfs *sysfs;
  BoltUdev  *udev;
  char      *rundir;
} TestPower;


static void
test_power_setup (TestPower *tt, gconstpointer data)
{
  g_autoptr(GError) err = NULL;

  tt->sysfs = mock_sysfs_new ();
  tt->udev = bolt_udev_new ("udev", NULL, &err);

  g_assert_no_error (err);
  g_assert_nonnull (tt->udev);

  tt->rundir = g_strdup (g_getenv ("BOLT_RUNDIR"));
  if (tt->rundir == NULL)
    tt->rundir = g_dir_make_tmp ("bolt.power.XXXXXX", &err);

  g_assert_no_error (err);
  g_assert_nonnull (tt->rundir);

  g_debug ("rundir at '%s'", tt->rundir);
}

static void
test_power_tear_down (TestPower *tt, gconstpointer user)
{
  g_autoptr(GError) err = NULL;
  gboolean ok;

  ok = bolt_fs_cleanup_dir (tt->rundir, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  g_clear_object (&tt->sysfs);
  g_clear_object (&tt->udev);
  g_clear_pointer (&tt->rundir, g_free);
}

static BoltPower *
make_bolt_power_timeout (TestPower *tt, guint timeout)
{
  g_autoptr(GError) err = NULL;
  BoltPower *power;

  power =  g_initable_new (BOLT_TYPE_POWER,
                           NULL, &err,
                           "udev", tt->udev,
                           "timeout", timeout,
                           "rundir", tt->rundir,
                           NULL);

  g_assert_no_error (err);
  g_assert_nonnull (power);

  return power;
}

static void
test_power_basic (TestPower *tt, gconstpointer user)
{
  g_autoptr(BoltPower) power = NULL;
  g_autoptr(GError) err = NULL;
  g_autoptr(BoltUdev) udev = NULL;
  g_autoptr(GFile) statedir = NULL;
  g_autoptr(BoltGuard) guard = NULL;
  g_autofree char *guard_id = NULL;
  g_autofree char *guard_who = NULL;
  g_autofree char *guard_path = NULL;
  g_autofree char *guard_fifo = NULL;
  g_autofree char *rundir = NULL;
  gulong guard_pid;
  BoltPowerState state;
  gboolean supported;
  gboolean on;
  const char *fp;
  guint timeout;

  power = make_bolt_power_timeout (tt, 0);

  g_object_get (power,
                "rundir", &rundir,
                "statedir", &statedir,
                "udev", &udev,
                "supported", &supported,
                "state", &state,
                "timeout", &timeout,
                NULL);

  g_assert_cmpstr (rundir, ==, tt->rundir);
  g_assert_nonnull (statedir);
  g_assert_nonnull (udev);
  g_assert (udev == tt->udev);
  g_assert_false (supported);
  g_assert (state == BOLT_FORCE_POWER_UNSET);
  state = bolt_power_get_state (power);
  g_assert (state == BOLT_FORCE_POWER_UNSET);
  g_assert_cmpuint (timeout, ==, 0);

  /* force power is unsupported, check the error handling */
  guard = bolt_power_acquire (power, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
  g_assert_null (guard);
  g_clear_pointer (&err, g_error_free);

  /* add the force power sysfs device,
   * now it must be reported as supported */
  g_object_unref (power);

  fp = mock_sysfs_force_power_add (tt->sysfs);
  g_assert_nonnull (fp);

  power = make_bolt_power_timeout (tt, 0);
  g_object_get (power,
                "supported", &supported,
                "state", &state,
                NULL);

  g_assert_true (supported);
  g_assert (state == BOLT_FORCE_POWER_UNSET);
  state = bolt_power_get_state (power);
  g_assert (state == BOLT_FORCE_POWER_UNSET);

  /* set of ON */
  guard = bolt_power_acquire (power, &err);
  g_assert_no_error (err);
  g_assert_nonnull (guard);

  g_object_get (power,
                "state", &state,
                NULL);

  g_assert (state == BOLT_FORCE_POWER_ON);

  state = bolt_power_get_state (power);
  g_assert (state == BOLT_FORCE_POWER_ON);
  state = bolt_power_get_state (power);
  g_assert (state == BOLT_FORCE_POWER_ON);

  on = mock_sysfs_force_power_enabled (tt->sysfs);
  g_assert_true (on);

  g_object_get (guard,
                "id", &guard_id,
                "who", &guard_who,
                "path", &guard_path,
                "pid", &guard_pid,
                "fifo", &guard_fifo,
                NULL);

  g_assert_nonnull (guard_id);
  g_assert_nonnull (guard_who);
  g_assert_nonnull (guard_path);

  g_assert_cmpstr (guard_id, ==, "1");
  g_assert_cmpstr (guard_who, ==, "boltd");
  g_assert_cmpuint (guard_pid, ==, getpid ());

  g_assert_cmpstr (guard_id, ==, bolt_guard_get_id (guard));
  g_assert_cmpstr (guard_who, ==, bolt_guard_get_who (guard));
  g_assert_cmpuint (guard_pid, ==, bolt_guard_get_pid (guard));

  /* set of OFF */
  g_clear_object (&guard);

  g_object_get (power,
                "state", &state,
                NULL);

  g_assert (state == BOLT_FORCE_POWER_OFF);
  state = bolt_power_get_state (power);
  g_assert (state == BOLT_FORCE_POWER_OFF);
  on = mock_sysfs_force_power_enabled (tt->sysfs);
  g_assert_false (on);
}

static void
test_power_multiple (TestPower *tt, gconstpointer user)
{
  g_autoptr(BoltPower) power = NULL;
  g_autoptr(GError) err = NULL;
  g_autoptr(BoltGuard) guard = NULL;
  GPtrArray *guards = NULL;
  BoltPowerState state;
  gboolean supported;
  gboolean on;
  const char *fp;

  fp = mock_sysfs_force_power_add (tt->sysfs);
  g_assert_nonnull (fp);

  power = make_bolt_power_timeout (tt, 0);
  g_object_get (power,
                "supported", &supported,
                "state", &state,
                NULL);

  g_assert_true (supported);
  g_assert (state == BOLT_FORCE_POWER_UNSET);

  on = mock_sysfs_force_power_enabled (tt->sysfs);
  g_assert_false (on);

  /* set to ON via first guard */
  guard = bolt_power_acquire (power, &err);
  g_assert_no_error (err);
  g_assert_nonnull (guard);

  state = bolt_power_get_state (power);
  g_assert (state == BOLT_FORCE_POWER_ON);
  on = mock_sysfs_force_power_enabled (tt->sysfs);
  g_assert_true (on);

  /* add one and remove it, nothing should change */
  for (guint i = 0; i < 5; i++)
    {
      g_autoptr(BoltGuard) g = NULL;

      state = bolt_power_get_state (power);
      g_assert (state == BOLT_FORCE_POWER_ON);
      on = mock_sysfs_force_power_enabled (tt->sysfs);
      g_assert_true (on);

      g = bolt_power_acquire (power, &err);

      g_assert_no_error (err);
      g_assert_nonnull (g);

      /* nothing should change */
      state = bolt_power_get_state (power);
      g_assert (state == BOLT_FORCE_POWER_ON);
      on = mock_sysfs_force_power_enabled (tt->sysfs);
      g_assert_true (on);
    }

  state = bolt_power_get_state (power);
  g_assert (state == BOLT_FORCE_POWER_ON);
  on = mock_sysfs_force_power_enabled (tt->sysfs);
  g_assert_true (on);

  /* set of OFF */
  g_clear_object (&guard);
  state = bolt_power_get_state (power);
  g_assert (state == BOLT_FORCE_POWER_OFF);
  on = mock_sysfs_force_power_enabled (tt->sysfs);
  g_assert_false (on);

  /* now all at once */
  guards = g_ptr_array_new_with_free_func (g_object_unref);
  for (guint i = 0; i < 5; i++)
    {
      BoltGuard *g = bolt_power_acquire (power, &err);
      g_ptr_array_add (guards, g);
    }

  state = bolt_power_get_state (power);
  g_assert (state == BOLT_FORCE_POWER_ON);
  on = mock_sysfs_force_power_enabled (tt->sysfs);
  g_assert_true (on);

  /* release all of the guards at once */
  g_clear_pointer (&guards, g_ptr_array_unref);
  state = bolt_power_get_state (power);
  g_assert (state == BOLT_FORCE_POWER_OFF);
  on = mock_sysfs_force_power_enabled (tt->sysfs);
  g_assert_false (on);
}

static void
on_notify_quit_loop (GObject    *gobject,
                     GParamSpec *pspec,
                     gpointer    user_data)
{

  GMainLoop *loop = user_data;

  g_main_loop_quit (loop);
}

static gboolean
on_timeout_warn_quit_loop (gpointer user_data)
{
  GMainLoop *loop = user_data;

  g_main_loop_quit (loop);
  g_warning ("timeout reached");
  return G_SOURCE_CONTINUE;
}

static void
test_power_timeout (TestPower *tt, gconstpointer user)
{
  g_autoptr(BoltPower) power = NULL;
  g_autoptr(GError) err = NULL;
  g_autoptr(BoltGuard) guard = NULL;
  g_autoptr(GMainLoop) loop = NULL;
  BoltPowerState state;
  gboolean supported;
  gboolean on;
  const char *fp;
  guint timeout;
  guint tid;

  fp = mock_sysfs_force_power_add (tt->sysfs);
  g_assert_nonnull (fp);

  /* non-zero timeout */
  power = make_bolt_power_timeout (tt, 10);

  g_object_get (power,
                "supported", &supported,
                "state", &state,
                "timeout", &timeout,
                NULL);

  g_assert_true (supported);
  g_assert (state == BOLT_FORCE_POWER_UNSET);
  g_assert_cmpuint (timeout, ==, 10);

  on = mock_sysfs_force_power_enabled (tt->sysfs);
  g_assert_false (on);

  /* set to ON ... */
  guard = bolt_power_acquire (power, &err);
  g_assert_no_error (err);
  g_assert_nonnull (guard);
  state = bolt_power_get_state (power);
  g_assert (state == BOLT_FORCE_POWER_ON);

  /* .. and OFF*/
  g_clear_object (&guard);

  /* but with a timeout, so we should be on still */
  state = bolt_power_get_state (power);
  g_assert (state == BOLT_FORCE_POWER_WAIT);
  on = mock_sysfs_force_power_enabled (tt->sysfs);
  g_assert_true (on);

  loop = g_main_loop_new (NULL, FALSE);
  tid = g_timeout_add_seconds (5, on_timeout_warn_quit_loop, loop);
  g_signal_connect (power, "notify::state",
                    G_CALLBACK (on_notify_quit_loop),
                    loop);

  /* now we wait for a state change */
  g_main_loop_run (loop);
  g_source_remove (tid);

  /* we should have one now */
  state = bolt_power_get_state (power);
  g_assert (state == BOLT_FORCE_POWER_OFF);
  on = mock_sysfs_force_power_enabled (tt->sysfs);
  g_assert_false (on);
}

static void
test_power_recover_state (TestPower *tt, gconstpointer user)
{
  g_autoptr(BoltPower) power = NULL;
  g_autoptr(BoltGuard) guard = NULL;
  g_autoptr(GError) err = NULL;
  BoltPowerState state;
  GFile *guarddir;
  const char *fp;

  fp = mock_sysfs_force_power_add (tt->sysfs);
  g_assert_nonnull (fp);

  if (g_test_subprocess ())
    {
      /* we are the subprocess, create a BoltPower instance
       * but simulate a non-clean shutdown */
      power = make_bolt_power_timeout (tt, 20 * 1000);

      g_assert_no_error (err);
      g_assert_nonnull (power);

      guard = bolt_power_acquire (power, &err);
      g_assert_no_error (err);
      g_assert_nonnull (guard);

      state = bolt_power_get_state (power);
      g_assert_cmpint (state, ==, BOLT_FORCE_POWER_ON);

      g_clear_object (&guard);
      state = bolt_power_get_state (power);
      g_assert_cmpint (state, ==, BOLT_FORCE_POWER_WAIT);

      g_debug ("simulating crashing boltd");
      exit (EXIT_SUCCESS);
    }

  // the main test
  g_setenv ("BOLT_RUNDIR", tt->rundir, TRUE);
  g_test_trap_subprocess (NULL, 0,
                          G_TEST_SUBPROCESS_INHERIT_STDOUT |
                          G_TEST_SUBPROCESS_INHERIT_STDERR);

  g_test_trap_assert_passed ();

  power = make_bolt_power_timeout (tt, 10);

  g_assert_no_error (err);
  g_assert_nonnull (power);

  guarddir = bolt_power_get_statedir (power);
  g_assert_nonnull (guarddir);

  state = bolt_power_get_state (power);
  g_assert_cmpint (state, ==, BOLT_FORCE_POWER_WAIT);

  g_unsetenv ("BOLT_RUNDIR");
}

static void
test_power_recover_guards_fail (TestPower *tt, gconstpointer user)
{
  g_autoptr(BoltPower) power = NULL;
  g_autoptr(BoltGuard) guard = NULL;
  g_autoptr(GError) err = NULL;
  BoltPowerState state;
  const char *fp;
  pid_t pid;
  int r;

#if HAVE_ASAN
  g_test_skip ("Test does not work with ASAN yet.");
  return;
#endif

  fp = mock_sysfs_force_power_add (tt->sysfs);
  g_assert_nonnull (fp);

  pid = fork ();
  g_assert_cmpint (pid, !=, -1);

  if (pid == 0)
    {
      /* child */
      power = make_bolt_power_timeout (tt, 10);

      g_assert_no_error (err);
      g_assert_nonnull (power);

      state = bolt_power_get_state (power);
      g_assert_cmpint (state, ==, BOLT_FORCE_POWER_UNSET);

      /* we pass in zero as pid, which means it will be our pid */
      guard = bolt_power_acquire_full (power, "test", 0, &err);
      g_assert_no_error (err);
      g_assert_nonnull (guard);

      state = bolt_power_get_state (power);
      g_assert_cmpint (state, ==, BOLT_FORCE_POWER_ON);

      exit (0);
    }

  /* parent */
  pid = waitpid (pid, &r, 0);
  g_assert_cmpint (pid, >, 0);
  g_assert_cmpint (r, ==, 0);

  /* now lets recover the guard */
  power = make_bolt_power_timeout (tt, 10);

  g_assert_no_error (err);
  g_assert_nonnull (power);

  state = bolt_power_get_state (power);
  g_assert_cmpint (state, ==, BOLT_FORCE_POWER_WAIT);
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
test_power_guards_fifo (TestPower *tt, gconstpointer user)
{
  g_autoptr(BoltPower) power = NULL;
  g_autoptr(GError) err = NULL;
  g_autoptr(BoltGuard) guard = NULL;
  g_autoptr(GMainLoop) loop = NULL;
  BoltPowerState state;
  gboolean on;
  const char *fp;
  guint tid;
  int fd;

  fp = mock_sysfs_force_power_add (tt->sysfs);
  g_assert_nonnull (fp);

  /* non-zero timeout */
  power = make_bolt_power_timeout (tt, 0);

  on = mock_sysfs_force_power_enabled (tt->sysfs);
  g_assert_false (on);

  /* set to ON ... */
  guard = bolt_power_acquire (power, &err);
  g_assert_no_error (err);
  g_assert_nonnull (guard);
  state = bolt_power_get_state (power);
  g_assert (state == BOLT_FORCE_POWER_ON);

  fd = bolt_guard_monitor (guard, &err);
  g_assert_no_error (err);
  g_assert_cmpint (fd, >, -1);

  /* we should still be ON and the guard still active,
   * because the event watcher still owns a reference
   */
  g_clear_object (&guard);
  state = bolt_power_get_state (power);
  g_assert_cmpint (state, ==, BOLT_FORCE_POWER_ON);

  loop = g_main_loop_new (NULL, FALSE);

  /* fail if we don't have anything after n seconds */
  tid = g_timeout_add_seconds (5, on_timeout_warn_quit_loop, loop);

  /* schedule a closing of the fifo */
  g_idle_add (on_cb_close_fd, (gpointer) & fd);

  g_signal_connect (power, "notify::state",
                    G_CALLBACK (on_notify_quit_loop),
                    loop);

  /* now we wait for the fifo to be closed */
  g_main_loop_run (loop);
  g_source_remove (tid);

  /* we should have one now */
  state = bolt_power_get_state (power);
  g_assert (state == BOLT_FORCE_POWER_OFF);
  on = mock_sysfs_force_power_enabled (tt->sysfs);
  g_assert_false (on);
}

static void
test_power_wmi_uevent (TestPower *tt, gconstpointer user)
{
  g_autoptr(BoltPower) power = NULL;
  g_autoptr(GMainLoop) loop = NULL;
  BoltPowerState state;
  gboolean supported;
  const char *fp;
  guint tid;

  /* now we add the wmi module */
  fp = mock_sysfs_force_power_add (tt->sysfs);
  g_assert_nonnull (fp);
  loop = g_main_loop_new (NULL, FALSE);
  power = make_bolt_power_timeout (tt, 0);

  g_object_get (power,
                "supported", &supported,
                "state", &state,
                NULL);

  g_assert_true (supported);
  g_assert_cmpint (state, ==, BOLT_FORCE_POWER_UNSET);
  state = bolt_power_get_state (power);
  g_assert_cmpint (state, ==, BOLT_FORCE_POWER_UNSET);


  /* UNLOAD */
  g_debug ("UNLOAD");
  mock_sysfs_force_power_unload (tt->sysfs);

  tid = g_timeout_add_seconds (5, on_timeout_warn_quit_loop, loop);
  g_signal_connect (power, "notify::supported",
                    G_CALLBACK (on_notify_quit_loop),
                    loop);

  /* we wait for a change in the state*/
  g_main_loop_run (loop);
  g_source_remove (tid);

  g_object_get (power,
                "supported", &supported,
                "state", &state,
                NULL);

  g_assert_false (supported);
  g_assert_cmpint (state, ==, BOLT_FORCE_POWER_UNSET);
  state = bolt_power_get_state (power);
  g_assert_cmpint (state, ==, BOLT_FORCE_POWER_UNSET);

  /* LOAD */
  g_debug ("LOAD");
  mock_sysfs_force_power_load (tt->sysfs);

  tid = g_timeout_add_seconds (5, on_timeout_warn_quit_loop, loop);
  g_signal_connect (power, "notify::state",
                    G_CALLBACK (on_notify_quit_loop),
                    loop);

  /* we wait for a change in the state*/
  g_main_loop_run (loop);
  g_source_remove (tid);

  g_object_get (power,
                "supported", &supported,
                "state", &state,
                NULL);

  g_assert_true (supported);
  g_assert_cmpint (state, ==, BOLT_FORCE_POWER_UNSET);
  state = bolt_power_get_state (power);
  g_assert_cmpint (state, ==, BOLT_FORCE_POWER_UNSET);
}

int
main (int argc, char **argv)
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  bolt_dbus_ensure_resources ();

  g_test_add ("/power/basic",
              TestPower,
              NULL,
              test_power_setup,
              test_power_basic,
              test_power_tear_down);

  g_test_add ("/power/mutli-guards",
              TestPower,
              NULL,
              test_power_setup,
              test_power_multiple,
              test_power_tear_down);

  g_test_add ("/power/timeout",
              TestPower,
              NULL,
              test_power_setup,
              test_power_timeout,
              test_power_tear_down);

  g_test_add ("/power/recover",
              TestPower,
              NULL,
              test_power_setup,
              test_power_recover_state,
              test_power_tear_down);

  g_test_add ("/power/guards/recover/fail",
              TestPower,
              NULL,
              test_power_setup,
              test_power_recover_guards_fail,
              test_power_tear_down);

  g_test_add ("/power/guards/fifo",
              TestPower,
              NULL,
              test_power_setup,
              test_power_guards_fifo,
              test_power_tear_down);

  g_test_add ("/power/wmi-uevent",
              TestPower,
              NULL,
              test_power_setup,
              test_power_wmi_uevent,
              test_power_tear_down);

  return g_test_run ();
}

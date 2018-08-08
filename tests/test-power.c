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

#include "bolt-str.h"
#include "mock-sysfs.h"

#include "bolt-daemon-resource.h"

#include <glib.h>
#include <gio/gio.h>
#include <glib/gprintf.h>
#include <glib/gstdio.h>

#include <libudev.h>
#include <locale.h>

typedef struct udev_device udev_device;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (udev_device, udev_device_unref);


typedef struct
{
  MockSysfs *sysfs;
  BoltUdev  *udev;
} TestPower;


static void
test_power_setup (TestPower *tt, gconstpointer data)
{
  g_autoptr(GError) err = NULL;

  tt->sysfs = mock_sysfs_new ();
  tt->udev = bolt_udev_new ("udev", NULL, &err);

  g_assert_no_error (err);
  g_assert_nonnull (tt->udev);
}

static void
test_power_tear_down (TestPower *tt, gconstpointer user)
{
  g_clear_object (&tt->sysfs);
  g_clear_object (&tt->udev);
}

static void
test_power_basic (TestPower *tt, gconstpointer user)
{
  g_autoptr(BoltPower) power = NULL;
  g_autoptr(GError) err = NULL;
  g_autoptr(BoltPowerGuard) guard = NULL;
  g_autofree char *guard_id = NULL;
  g_autofree char *guard_who = NULL;
  BoltPowerState state;
  gboolean supported;
  gboolean on;
  const char *fp;

  power = bolt_power_new (tt->udev);

  g_object_get (power,
                "supported", &supported,
                "state", &state,
                NULL);

  g_assert_false (supported);
  g_assert (state == BOLT_FORCE_POWER_UNSET);
  state = bolt_power_get_state (power);
  g_assert (state == BOLT_FORCE_POWER_UNSET);

  /* add the force power sysfs device,
   * now it must be reported as supported */
  g_object_unref (power);

  fp = mock_sysfs_force_power_add (tt->sysfs);
  g_assert_nonnull (fp);

  power = bolt_power_new (tt->udev);
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
                NULL);

  g_assert_nonnull (guard_id);
  g_assert_nonnull (guard_who);

  g_assert_cmpstr (guard_id, ==, "1");
  g_assert_cmpstr (guard_who, ==, "boltd");

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
  g_autoptr(BoltPowerGuard) guard = NULL;
  GPtrArray *guards = NULL;
  BoltPowerState state;
  gboolean supported;
  gboolean on;
  const char *fp;

  power = bolt_power_new (tt->udev);

  fp = mock_sysfs_force_power_add (tt->sysfs);
  g_assert_nonnull (fp);

  power = bolt_power_new (tt->udev);
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
      g_autoptr(BoltPowerGuard) g = NULL;

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
      BoltPowerGuard *g = bolt_power_acquire (power, &err);
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

int
main (int argc, char **argv)
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  g_resources_register (bolt_daemon_get_resource ());

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


  return g_test_run ();
}
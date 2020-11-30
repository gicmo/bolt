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

#include "bolt-udev.h"

#include "bolt-dbus.h"
#include "bolt-str.h"
#include "bolt-test.h"
#include "mock-sysfs.h"

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
  MockSysfs   *sysfs;
  struct udev *udev;
} TestUdev;


static void
test_udev_setup (TestUdev *tt, gconstpointer data)
{
  tt->sysfs = mock_sysfs_new ();
  tt->udev = udev_new ();
}

static void
test_udev_tear_down (TestUdev *tt, gconstpointer user)
{
  g_clear_object (&tt->sysfs);
  g_clear_pointer (&tt->udev, udev_unref);
}

typedef struct
{

  char        *action;
  udev_device *dev;

  gint         have;

  /*  */
  GMainLoop *loop;

  /* conditions */
  gint     should;
  gboolean timedout;

} UEvent;

static void
uevent_clear (UEvent *ev)
{
  g_clear_pointer (&ev->dev, udev_device_unref);
  g_clear_pointer (&ev->action, g_free);

  g_clear_pointer (&ev->loop, g_main_loop_unref);
}

static void
got_uevent (BoltUdev           *udev,
            const char         *action,
            struct udev_device *device,
            gpointer            user_data)
{
  UEvent *ev = user_data;
  gboolean quit = FALSE;

  bolt_set_strdup (&ev->action, action);
  g_clear_pointer (&ev->dev, udev_device_unref);
  ev->dev = udev_device_ref (device);

  ev->have++;

  if (ev->should > 0)
    quit = (--ev->should) == 0;

  if (quit)
    g_main_loop_quit (ev->loop);
}

static gboolean
got_timeout (gpointer user_data)
{
  UEvent *ev = user_data;

  ev->timedout = TRUE;
  g_main_loop_quit (ev->loop);

  return FALSE;
}

static gint
wait_for_event (UEvent *ev, guint timeout)
{
  guint tid;

  ev->timedout = FALSE;
  tid = g_timeout_add_seconds (timeout, got_timeout, ev);

  if (ev->loop == NULL)
    ev->loop = g_main_loop_new (NULL, FALSE);

  ev->should = 1;
  ev->have = 0;
  g_main_loop_run (ev->loop);

  if (!ev->timedout)
    g_source_remove (tid);

  return ev->have;
}

static void
test_udev_basic (TestUdev *tt, gconstpointer user)
{
  g_auto(GStrv) prop_filter = NULL;
  g_autoptr(GError) err = NULL;
  g_autoptr(BoltUdev) udev = NULL;
  struct udev_device *dev = NULL;
  g_autofree char *prop_name = NULL;
  g_autofree char *idstr = NULL;
  UEvent ev = { NULL, };
  const char *filter[] = {"thunderbolt", NULL};
  const char *syspath;
  const char *domain;
  const char *name;
  gint n;

  udev = bolt_udev_new ("udev", filter, &err);

  g_assert_nonnull (udev);
  g_assert_no_error (err);

  g_object_get (udev,
                "name", &prop_name,
                "filter", &prop_filter,
                NULL);

  g_assert_cmpstr (prop_name, ==, "udev");
  bolt_assert_strv_equal ((const GStrv) filter, prop_filter, -1);

  g_signal_connect (udev, "uevent", (GCallback) got_uevent, &ev);

  /* add a domain */
  domain = mock_sysfs_domain_add (tt->sysfs, BOLT_SECURITY_NONE, NULL);
  n = wait_for_event (&ev, 2);

  g_assert_false (ev.timedout);
  g_assert_cmpint (n, ==, 1);
  g_assert_cmpstr (ev.action, ==, "add");
  g_assert_nonnull (ev.dev);

  name = udev_device_get_sysname (ev.dev);
  g_assert_cmpstr (domain, ==, name);

  /* test we can create a valid udev device */
  syspath = udev_device_get_syspath (ev.dev);

  dev = bolt_udev_device_new_from_syspath (udev, syspath, &err);
  g_assert_nonnull (dev);
  g_assert_no_error (err);

  /* remove a domain */
  idstr = g_strdup (domain);
  mock_sysfs_domain_remove (tt->sysfs, idstr);
  n = wait_for_event (&ev, 2);

  g_assert_false (ev.timedout);
  g_assert_cmpint (n, ==, 1);
  g_assert_cmpstr (ev.action, ==, "remove");
  g_assert_nonnull (ev.dev);

  name = udev_device_get_sysname (ev.dev);
  g_assert_cmpstr (idstr, ==, name);

  /* cleanup */
  uevent_clear (&ev);
  udev_device_unref (dev);
}

static void
test_udev_detect_force_power (TestUdev *tt, gconstpointer user)
{
  g_autoptr(GError) err = NULL;
  g_autoptr(BoltUdev) udev = NULL;
  g_autofree char *path = NULL;
  const char *fp;
  gboolean ok;

  udev = bolt_udev_new ("udev", NULL, &err);

  g_assert_no_error (err);
  g_assert_nonnull (udev);

  /* no force power module attached so far */

  ok = bolt_udev_detect_force_power (udev, &path, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  g_assert_null (path);

  /* now we add the wmi module */
  fp = mock_sysfs_force_power_add (tt->sysfs);
  g_assert_nonnull (fp);

  ok = bolt_udev_detect_force_power (udev, &path, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  g_assert_nonnull (path);
  g_debug ("force power detected at: %s", path);
  g_clear_pointer (&path, g_free);

  /* unload again */
  g_debug ("UNLOAD");
  mock_sysfs_force_power_unload (tt->sysfs);

  ok = bolt_udev_detect_force_power (udev, &path, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  g_assert_null (path);

  /* and load again */
  g_debug ("LOAD");
  mock_sysfs_force_power_load (tt->sysfs);

  ok = bolt_udev_detect_force_power (udev, &path, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  g_assert_nonnull (path);
  g_debug ("force power detected at: %s", path);
}

int
main (int argc, char **argv)
{

  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  bolt_dbus_ensure_resources ();

  g_test_add ("/udev/basic",
              TestUdev,
              NULL,
              test_udev_setup,
              test_udev_basic,
              test_udev_tear_down);

  g_test_add ("/udev/detect_force_power",
              TestUdev,
              NULL,
              test_udev_setup,
              test_udev_detect_force_power,
              test_udev_tear_down);

  return g_test_run ();
}

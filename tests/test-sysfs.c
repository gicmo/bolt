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

#include "bolt-str.h"
#include "bolt-sysfs.h"
#include "bolt-domain.h"

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
  MockSysfs   *sysfs;
  struct udev *udev;
} TestSysfs;


static void
test_sysfs_setup (TestSysfs *tt, gconstpointer data)
{
  tt->sysfs = mock_sysfs_new ();
  tt->udev = udev_new ();
}

static void
test_sysfs_tear_down (TestSysfs *tt, gconstpointer user)
{
  g_clear_object (&tt->sysfs);
  g_clear_pointer (&tt->udev, udev_unref);
}

static void
count_domains (gpointer data,
               gpointer user_data)
{
  int *n = user_data;

  (*n)++;
}

static void
test_sysfs_domain_for_device (TestSysfs *tt, gconstpointer user)
{
  g_autoptr(udev_device) udevice = NULL;
  udev_device *dh;
  udev_device *dd;
  const char *domain;
  const char *host;
  const char *dock;
  const char *syspath;
  MockDevId hostid = {
    .vendor_id = 0x42,
    .vendor_name = "GNOME.org",
    .device_id = 0x42,
    .device_name = "Laptop",
    .unique_id = "884c6edd-7118-4b21-b186-b02d396ecca0",
  };
  MockDevId dockid = {
    .vendor_id = 0x42,
    .vendor_name = "GNOME.org",
    .device_id = 0x42,
    .device_name = "Thunderbolt Dock",
    .unique_id = "884c6edd-7118-4b21-b186-b02d396ecca1",
  };

  domain = mock_sysfs_domain_add (tt->sysfs, BOLT_SECURITY_SECURE, NULL);
  g_assert_nonnull (domain);

  host = mock_sysfs_host_add (tt->sysfs, domain, &hostid);
  g_assert_nonnull (host);

  dock = mock_sysfs_device_add (tt->sysfs,
                                host,
                                &dockid,
                                0,
                                NULL,
                                0);

  g_assert_nonnull (dock);

  syspath = mock_sysfs_device_get_syspath (tt->sysfs, dock);
  udevice = udev_device_new_from_syspath (tt->udev, syspath);
  g_assert_nonnull (udevice);

  /* for the dock */
  dd = bolt_sysfs_domain_for_device (udevice, &dh);
  g_assert_nonnull (dd);
  g_assert_nonnull (dh);

  g_assert_cmpstr (udev_device_get_syspath (dd),
                   ==,
                   mock_sysfs_domain_get_syspath (tt->sysfs, domain));

  g_assert_cmpstr (udev_device_get_syspath (dh),
                   ==,
                   mock_sysfs_device_get_syspath (tt->sysfs, host));

  /* for the host itself */
  dd = bolt_sysfs_domain_for_device (dh, &dh);
  g_assert_nonnull (dd);
  g_assert_nonnull (dh);

  g_assert_cmpstr (udev_device_get_syspath (dd),
                   ==,
                   mock_sysfs_domain_get_syspath (tt->sysfs, domain));

  g_assert_cmpstr (udev_device_get_syspath (dh),
                   ==,
                   mock_sysfs_device_get_syspath (tt->sysfs, host));

  mock_sysfs_domain_remove (tt->sysfs, domain);
}

static void
test_sysfs_domains (TestSysfs *tt, gconstpointer user)
{
  g_autoptr(GError) err = NULL;
  const char *uid = "884c6edd-7118-4b21-b186-b02d396ecca0";
  const char *ids[5];
  BoltSecurity sl[5] = {BOLT_SECURITY_NONE,
                        BOLT_SECURITY_DPONLY,
                        BOLT_SECURITY_USER,
                        BOLT_SECURITY_SECURE,
                        BOLT_SECURITY_USBONLY};
  BoltDomain *all[5] = {NULL, };
  BoltDomain *domains = NULL;
  BoltDomain *iter;
  int n;

  n = bolt_sysfs_count_domains (tt->udev, &err);

  g_assert_no_error (err);
  g_assert_cmpint (n, ==, 0);

  for (gsize i = 0; i < G_N_ELEMENTS (sl); i++)
    {
      g_autoptr(udev_device) udevice = NULL;
      g_autoptr(BoltDomain) dom = NULL; /* the list will own reference */
      const char *syspath;

      ids[i] = mock_sysfs_domain_add (tt->sysfs, sl[i], NULL);

      syspath = mock_sysfs_domain_get_syspath (tt->sysfs, ids[i]);
      udevice = udev_device_new_from_syspath (tt->udev, syspath);

      g_assert_nonnull (udevice);

      dom = bolt_domain_new_for_udev (udevice, uid, &err);
      g_assert_no_error (err);
      g_assert_nonnull (dom);

      g_assert_cmpstr (syspath, ==, bolt_domain_get_syspath (dom));
      g_assert_cmpstr (uid, ==, bolt_domain_get_uid (dom));
      g_assert_cmpstr (ids[i], ==, bolt_domain_get_id (dom));

      domains = bolt_domain_insert (domains, dom);
      all[i] = dom;
      g_object_add_weak_pointer (G_OBJECT (dom), (gpointer *) &all[i]);
    }

  g_assert_nonnull (domains);
  g_assert_cmpuint (bolt_domain_count (domains),
                    ==,
                    G_N_ELEMENTS (sl));

  g_assert_cmpint (bolt_sysfs_count_domains (tt->udev, NULL),
                   ==,
                   (int) G_N_ELEMENTS (sl));

  n = 0;
  bolt_domain_foreach (domains, count_domains, &n);
  g_assert_cmpint (n, ==, (int) G_N_ELEMENTS (sl));

  iter = domains;
  for (gsize i = 0; i < bolt_domain_count (domains); i++)
    {
      const char *id = bolt_domain_get_id (iter);
      BoltSecurity s = bolt_domain_get_security (iter);

      g_assert_cmpstr (id, ==, ids[i]);
      g_assert_cmpint (s, ==, sl[i]);

      iter = bolt_domain_next (iter);
    }

  iter = bolt_domain_prev (domains);
  g_assert_cmpstr (bolt_domain_get_id (iter),
                   ==,
                   ids[G_N_ELEMENTS (sl) - 1]);

  /* removing of domains: start with the second one */
  iter = bolt_domain_next (domains);
  domains = bolt_domain_remove (domains, iter);

  g_assert_cmpuint (bolt_domain_count (domains),
                    ==,
                    G_N_ELEMENTS (sl) - 1);

  g_assert_cmpstr (bolt_domain_get_id (domains), ==, ids[0]);

  iter = bolt_domain_next (domains);
  /* ids[1] should be gone */
  g_assert_cmpstr (bolt_domain_get_id (iter), ==, ids[2]);

  /* remove of domains: the list head */
  domains = bolt_domain_remove (domains, domains);
  g_assert_cmpuint (bolt_domain_count (domains),
                    ==,
                    G_N_ELEMENTS (sl) - 2);
  /* the head is now ids[2], because 0, 1 got removed */
  g_assert_cmpstr (bolt_domain_get_id (domains), ==, ids[2]);

  /* remove of domains: clear the whole list */
  bolt_domain_clear (&domains);
  g_assert_null (domains);
  g_assert_cmpuint (bolt_domain_count (domains), ==, 0);

  /* check we also got rid of all references */
  for (gsize i = 0; i < G_N_ELEMENTS (all); i++)
    g_assert_null (all[i]);
}

static void
test_sysfs_domain_bootacl (TestSysfs *tt, gconstpointer user)
{
  g_auto(GStrv) acl = NULL;
  g_auto(GStrv) have = NULL;
  g_autoptr(BoltDomain) dom = NULL;
  g_autoptr(udev_device) udevice = NULL;
  g_autoptr(GError) err = NULL;
  g_autofree const char **used = NULL;
  g_autofree char *str = NULL;
  const char *uid = "884c6edd-7118-4b21-b186-b02d396ecca0";
  const char *syspath;
  const char *d;
  guint slots = 16;
  guint n, n_free;
  guint n_used = 0;

  str = g_strnfill (slots - 1, ',');
  acl = g_strsplit (str, ",", 1024);

  g_assert_cmpuint (g_strv_length (acl), ==, slots);
  d = mock_sysfs_domain_add (tt->sysfs, BOLT_SECURITY_USER, acl);

  syspath = mock_sysfs_domain_get_syspath (tt->sysfs, d);
  udevice = udev_device_new_from_syspath (tt->udev, syspath);

  dom = bolt_domain_new_for_udev (udevice, uid, &err);
  g_assert_no_error (err);
  g_assert_nonnull (dom);

  g_object_get (dom, "bootacl", &have, NULL);
  g_assert_nonnull (have);

  g_assert_cmpuint (g_strv_length (acl),
                    ==,
                    g_strv_length (have));
  g_assert_true (bolt_strv_equal (acl, have));

  g_assert_true (bolt_domain_supports_bootacl (dom));

  n = bolt_domain_bootacl_slots (dom, &n_free);
  g_assert_cmpuint (n, ==, slots);
  g_assert_cmpuint (n_free, ==, slots);

  used = bolt_domain_bootacl_get_used (dom, &n_used);
  g_assert_cmpuint (g_strv_length ((GStrv) used), ==, 0);
  g_assert_cmpuint (n_used, ==, 0);
}

int
main (int argc, char **argv)
{

  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  g_resources_register (bolt_daemon_get_resource ());

  g_test_add ("/sysfs/domain_for_device",
              TestSysfs,
              NULL,
              test_sysfs_setup,
              test_sysfs_domain_for_device,
              test_sysfs_tear_down);

  g_test_add ("/sysfs/domain/basic",
              TestSysfs,
              NULL,
              test_sysfs_setup,
              test_sysfs_domains,
              test_sysfs_tear_down);

  g_test_add ("/sysfs/domain/bootacl",
              TestSysfs,
              NULL,
              test_sysfs_setup,
              test_sysfs_domain_bootacl,
              test_sysfs_tear_down);

  return g_test_run ();
}

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

#include "bolt-dbus.h"
#include "bolt-macros.h"
#include "bolt-store.h"
#include "bolt-str.h"
#include "bolt-sysfs.h"
#include "bolt-domain.h"

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
                                0,
                                NULL);

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
test_sysfs_info_for_device (TestSysfs *tt, gconstpointer user)
{
  g_autoptr(udev_device) udevice = NULL;
  g_autoptr(GError) err = NULL;
  const char *domain;
  const char *host;
  const char *dock;
  const char *syspath;
  BoltDevInfo info;
  gboolean ok;
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
  BoltLinkSpeed ls = {
    .rx.speed = 10,
    .rx.lanes = 1,
    .tx.speed = 20,
    .tx.lanes = 2
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
                                0,
                                &ls);


  syspath = mock_sysfs_device_get_syspath (tt->sysfs, dock);
  udevice = udev_device_new_from_syspath (tt->udev, syspath);
  g_assert_nonnull (udevice);

  ok = bolt_sysfs_info_for_device (udevice, TRUE, &info, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  g_assert_true (info.full);
  g_assert_cmpstr (info.parent, ==, hostid.unique_id);
  g_assert_cmpstr (info.syspath, ==, syspath);

  g_assert_cmpuint (info.linkspeed.rx.speed, ==, ls.rx.speed);
  g_assert_cmpuint (info.linkspeed.rx.lanes, ==, ls.rx.lanes);
  g_assert_cmpuint (info.linkspeed.tx.speed, ==, ls.tx.speed);
  g_assert_cmpuint (info.linkspeed.tx.lanes, ==, ls.tx.lanes);
}

static void
test_sysfs_read_iommu (TestSysfs *tt, gconstpointer user)
{
  const char *domain;
  const char *syspath;
  gboolean ok;

  domain = mock_sysfs_domain_add (tt->sysfs, BOLT_SECURITY_SECURE, NULL);
  g_assert_nonnull (domain);

  syspath = mock_sysfs_domain_get_syspath (tt->sysfs, domain);
  g_assert_nonnull (syspath);

  /* no sysfs attribute at all */
  {
    g_autoptr(GError) err = NULL;
    g_autoptr(udev_device) udev = NULL;
    gboolean iommu;

    udev = udev_device_new_from_syspath (tt->udev, syspath);
    g_assert_nonnull (udev);

    iommu = TRUE; /* we expect FALSE */
    ok = bolt_sysfs_read_iommu (udev, &iommu, &err);
    g_assert_no_error (err);
    g_assert_true (ok);
    g_assert_false (iommu);
  }

  /* sysfs attribute is "0" */
  {
    g_autoptr(GError) err = NULL;
    g_autoptr(udev_device) udev = NULL;
    gboolean iommu;

    ok = mock_syfs_domain_iommu_set (tt->sysfs, domain, "0", &err);
    g_assert_no_error (err);
    g_assert_true (ok);

    udev = udev_device_new_from_syspath (tt->udev, syspath);
    g_assert_nonnull (udev);

    iommu = TRUE; /* we expect FALSE */
    ok = bolt_sysfs_read_iommu (udev, &iommu, &err);
    g_assert_no_error (err);
    g_assert_true (ok);
    g_assert_false (iommu);
  }

  /* sysfs attribute is "1" */
  {
    g_autoptr(GError) err = NULL;
    g_autoptr(udev_device) udev = NULL;
    gboolean iommu;

    ok = mock_syfs_domain_iommu_set (tt->sysfs, domain, "1", &err);
    g_assert_no_error (err);
    g_assert_true (ok);

    udev = udev_device_new_from_syspath (tt->udev, syspath);
    g_assert_nonnull (udev);

    iommu = FALSE; /* now we expect TRUE */
    ok = bolt_sysfs_read_iommu (udev, &iommu, &err);
    g_assert_no_error (err);
    g_assert_true (ok);
    g_assert_true (iommu);
  }

  /* sysfs attribute contains garbage */
  {
    g_autoptr(GError) err = NULL;
    g_autoptr(udev_device) udev = NULL;
    gboolean iommu;

    ok = mock_syfs_domain_iommu_set (tt->sysfs, domain, "garbage", &err);
    g_assert_no_error (err);
    g_assert_true (ok);

    udev = udev_device_new_from_syspath (tt->udev, syspath);
    g_assert_nonnull (udev);

    iommu = TRUE; /* should be unchanged */
    ok = bolt_sysfs_read_iommu (udev, &iommu, &err);
    g_assert_nonnull (err);
    g_assert_false (ok);
    g_assert_true (iommu);
  }

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
test_sysfs_domain_connect (TestSysfs *tt, gconstpointer user)
{
  g_autoptr(udev_device) udevice = NULL;
  g_autoptr(BoltDomain) domain = NULL;
  g_autoptr(GError) err = NULL;
  const char *uid = "884c6edd-7118-4b21-b186-b02d396ecca0";
  const char *id;
  const char *syspath;
  BoltSecurity security;

  domain = g_object_new (BOLT_TYPE_DOMAIN,
                         "store", NULL,
                         "uid", uid,
                         "bootacl", NULL,
                         NULL);

  g_assert_nonnull (domain);
  g_assert_false (bolt_domain_has_iommu (domain));
  security = bolt_domain_get_security (domain);
  g_assert_cmpint (security, ==, BOLT_SECURITY_UNKNOWN);
  g_assert_null (bolt_domain_get_syspath (domain));

  id = mock_sysfs_domain_add (tt->sysfs,
                              BOLT_SECURITY_SECURE,
                              "iommu", "1\n",
                              NULL);

  syspath = mock_sysfs_domain_get_syspath (tt->sysfs, id);
  udevice = udev_device_new_from_syspath (tt->udev, syspath);

  g_assert_nonnull (udevice);

  bolt_domain_connected (domain, udevice);
  security = bolt_domain_get_security (domain);
  g_assert_cmpint (security, ==, BOLT_SECURITY_SECURE);

  g_assert_cmpstr (syspath,
                   ==,
                   bolt_domain_get_syspath (domain));

  g_assert_true (bolt_domain_has_iommu (domain));
}

typedef struct
{
  MockSysfs   *sysfs;
  struct udev *udev;

  GStrv        acl;
  guint        slots;

  const char  *dom_sysid;
  const char  *dom_uid;

  BoltDomain  *dom;
} TestBootacl;


static void
test_bootacl_setup (TestBootacl *tt, gconstpointer data)
{
  g_autoptr(GError) err = NULL;
  g_autoptr(udev_device) udevice = NULL;
  g_auto(GStrv) have = NULL;
  g_autofree char *str = NULL;
  const char *syspath;
  static const char *uid = "884c6edd-7118-4b21-b186-b02d396ecca0";

  tt->sysfs = mock_sysfs_new ();
  tt->udev = udev_new ();

  tt->slots = 16;

  str = g_strnfill (tt->slots - 1, ',');
  tt->acl = g_strsplit (str, ",", 1024);

  tt->dom_sysid = mock_sysfs_domain_add (tt->sysfs, BOLT_SECURITY_USER,
                                         "bootacl", tt->acl, NULL);

  syspath = mock_sysfs_domain_get_syspath (tt->sysfs, tt->dom_sysid);
  udevice = udev_device_new_from_syspath (tt->udev, syspath);

  tt->dom_uid = uid;
  tt->dom = bolt_domain_new_for_udev (udevice, tt->dom_uid, &err);
  g_assert_no_error (err);
  g_assert_nonnull (tt->dom);

  g_assert_cmpstr (bolt_domain_get_uid (tt->dom), ==, uid);

  g_assert_true (bolt_domain_supports_bootacl (tt->dom));
  g_object_get (tt->dom, "bootacl", &have, NULL);
  g_assert_nonnull (have);

  bolt_assert_strv_equal (have, tt->acl, -1);
}

static void
test_bootacl_tear_down (TestBootacl *tt, gconstpointer user)
{

  g_clear_pointer (&tt->acl, g_strfreev);
  g_clear_object (&tt->dom);

  g_clear_object (&tt->sysfs);
  g_clear_pointer (&tt->udev, udev_unref);
}


static void
dump_strv (GStrv strv, const char *prefix)
{
  if (strv == NULL)
    {
      g_print ("%s is NULL\n", prefix);
      return;
    }
  else if (*strv == NULL)
    {
      g_print ("%s is EMPTY\n", prefix);
      return;
    }

  for (guint i = 0; strv[i]; i++)
    g_print ("%s[%u] %s\n", prefix, i, strv[i]);
}

static void
test_bootacl_connect_domain (TestBootacl *tt, BoltDomain *dom)
{
  g_autoptr(udev_device) udevice = NULL;
  const char *syspath;

  syspath = mock_sysfs_domain_get_syspath (tt->sysfs, tt->dom_sysid);
  udevice = udev_device_new_from_syspath (tt->udev, syspath);
  bolt_domain_connected (dom, udevice);
}

static void
test_bootacl_read_acl (TestBootacl *tt, GStrv *acl)
{
  g_autoptr(GError) err = NULL;
  g_clear_pointer (acl, g_strfreev);

  *acl = mock_sysfs_domain_bootacl_get (tt->sysfs, tt->dom_sysid, &err);

  g_assert_no_error (err);
  g_assert_nonnull (*acl);
}

static void
test_bootacl_write_acl (TestBootacl *tt, GStrv acl)
{
  g_autoptr(GError) err = NULL;
  gboolean ok;

  ok = mock_sysfs_domain_bootacl_set (tt->sysfs, tt->dom_sysid, acl, &err);
  g_assert_no_error (err);
  g_assert_true (ok);
}

static void
test_bootacl_connect_and_verify (TestBootacl *tt,
                                 BoltDomain  *dom,
                                 GStrv       *acl)
{
  g_auto(GStrv) sysacl = NULL;
  GStrv have = NULL;

  test_bootacl_connect_domain (tt, dom);
  test_bootacl_read_acl (tt, &sysacl);

  dump_strv (sysacl, "sysacl ");
  dump_strv (tt->acl, "acl ");

  /* the domain and sysfs */
  have = bolt_domain_get_bootacl (dom);
  bolt_assert_strv_equal (have, sysacl, -1);

  /* the domain and what we expect */
  bolt_assert_strv_equal (have, tt->acl, -1);

  if (acl != NULL)
    bolt_swap (*acl, sysacl);
}

static void test_bootacl_add_uuid (TestBootacl *tt,
                                   BoltDomain  *dom,
                                   int          slot,
                                   const char  *uuidfmt,
                                   ...) G_GNUC_PRINTF (4, 5);

static void
test_bootacl_add_uuid (TestBootacl *tt,
                       BoltDomain  *dom,
                       int          slot,
                       const char  *uuidfmt,
                       ...)
{
  g_autoptr(GError) err = NULL;
  g_autofree char *uuid = NULL;
  GStrv acl = tt->acl;
  gboolean ok;
  va_list args;

  va_start (args, uuidfmt);

  uuid = g_strdup_vprintf (uuidfmt, args);
  ok = bolt_domain_bootacl_add (dom, uuid, &err);

  va_end (args);

  g_assert_no_error (err);
  g_assert_true (ok);

  g_assert_true (bolt_domain_bootacl_contains (dom, uuid));

  if (slot > -1)
    bolt_swap (acl[slot], uuid);
}

static void
test_bootacl_del_uuid (TestBootacl *tt,
                       BoltDomain  *dom,
                       const char  *uuid)
{
  g_autoptr(GError) err = NULL;
  GStrv acl = tt->acl;
  gboolean ok;
  char **p;

  ok = bolt_domain_bootacl_del (dom, uuid, &err);

  g_assert_no_error (err);
  g_assert_true (ok);

  g_assert_false (bolt_domain_bootacl_contains (dom, uuid));

  p = bolt_strv_contains (acl, uuid);
  if (p != NULL)
    bolt_set_strdup (p, "");
}

/* the bootacl related tests */

static void
test_bootacl_basic (TestBootacl *tt, gconstpointer user)
{
  g_auto(GStrv) sysacl = NULL;
  g_autofree const char **used = NULL;
  BoltDomain *dom = tt->dom;
  guint slots = tt->slots;
  guint n, n_free, n_used;

  g_assert_true (bolt_domain_supports_bootacl (dom));

  n = bolt_domain_bootacl_slots (dom, &n_free);
  g_assert_cmpuint (n, ==, slots);
  g_assert_cmpuint (n_free, ==, slots);

  used = bolt_domain_bootacl_get_used (dom, &n_used);
  g_assert_cmpuint (g_strv_length ((GStrv) used), ==, 0);
  g_assert_cmpuint (n_used, ==, 0);

  /* disconnect and reconnect */
  bolt_domain_disconnected (dom);
  test_bootacl_connect_and_verify (tt, dom, &sysacl);

  /* simulate some pathological cases that should not
   * actually happen, but should still be handled
   */

  /* after we connect, the slot list is empty */
  bolt_domain_disconnected (dom);
  g_strfreev (tt->acl);
  tt->acl = g_strsplit ("", ",", -1);

  test_bootacl_write_acl (tt, tt->acl);

  test_bootacl_connect_and_verify (tt, dom, &sysacl);
  g_assert_false (bolt_domain_supports_bootacl (dom));

  n = bolt_domain_bootacl_slots (dom, &n_free);
  g_assert_cmpuint (n, ==, 0);
  g_assert_cmpuint (n_free, ==, 0);

  /* after we connect, the slot list changed */
  bolt_domain_disconnected (dom);
  g_strfreev (tt->acl);
  tt->acl = g_strsplit (",", ",", -1); /* two slots */

  test_bootacl_write_acl (tt, tt->acl);

  test_bootacl_connect_and_verify (tt, dom, &sysacl);
  g_assert_true (bolt_domain_supports_bootacl (dom));

  n = bolt_domain_bootacl_slots (dom, &n_free);
  g_assert_cmpuint (n, ==, 2);
  g_assert_cmpuint (n_free, ==, 2);
}

static void
test_bootacl_errors (TestBootacl *tt, gconstpointer user)
{
  g_autoptr(udev_device) udevice = NULL;
  g_autoptr(BoltDomain) dom2 = NULL;
  g_autoptr(GError) err = NULL;
  g_auto(GStrv) tmp = NULL;
  g_autofree char *str = NULL;
  BoltDomain *dom = tt->dom;
  const char *noacl_dom;
  const char *syspath;
  GStrv acl = tt->acl;
  gboolean ok;

  /* adding an existing uuid */
  test_bootacl_add_uuid (tt, dom, 0, "deadbab%x-0200-0100-ffff-ffffffffffff", 0U);
  ok = bolt_domain_bootacl_add (dom, acl[0], &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_EXISTS);
  g_assert_false (ok);
  g_clear_pointer (&err, g_error_free);

  /* removing an unknown uuid */
  ok = bolt_domain_bootacl_del (dom, "deadbabe-0200-ffff-ffff-ffffffffffff", &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_assert_false (ok);
  g_clear_pointer (&err, g_error_free);

  /* number of slots mismatch */
  str = g_strnfill (tt->slots * 2, ',');
  tmp = g_strsplit (str, ",", 1024);

  ok = bolt_domain_bootacl_set (dom, tmp, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  g_assert_false (ok);
  g_clear_pointer (&err, g_error_free);

  /* domain without bootacl support */
  noacl_dom = mock_sysfs_domain_add (tt->sysfs, BOLT_SECURITY_SECURE, NULL);
  g_assert_nonnull (noacl_dom);

  syspath = mock_sysfs_domain_get_syspath (tt->sysfs, noacl_dom);
  udevice = udev_device_new_from_syspath (tt->udev, syspath);

  dom2 = bolt_domain_new_for_udev (udevice, tt->dom_uid, &err);
  g_assert_no_error (err);
  g_assert_nonnull (dom2);
  g_assert_false (bolt_domain_supports_bootacl (dom2));

  ok = bolt_domain_bootacl_add (dom2, acl[0], &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
  g_assert_false (ok);
  g_clear_pointer (&err, g_error_free);

  ok = bolt_domain_bootacl_del (dom2, acl[0], &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
  g_assert_false (ok);
  g_clear_pointer (&err, g_error_free);

  ok = bolt_domain_bootacl_set (dom2, tmp, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED);
  g_assert_false (ok);
  g_clear_pointer (&err, g_error_free);

}

static void
on_bootacl_notify (GObject    *object,
                   GParamSpec *pspec,
                   gpointer    user_data)
{
  guint *n_signals = user_data;

  (*n_signals)++;
}

typedef struct
{
  gboolean    changed;
  GHashTable *changes;
  gboolean    fired;
} AclChangeSet;

#define ACL_CHANGE_SET_INIT {FALSE, NULL, FALSE, }

static void
acl_change_set_clear (AclChangeSet *set)
{
  g_clear_pointer (&set->changes, g_hash_table_unref);
  set->changed = FALSE;
  set->fired = FALSE;
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (AclChangeSet, acl_change_set_clear);

static void
acl_change_set_verify (AclChangeSet *cs,
                       int           n_changes,
                       ...)
{
  guint sz;
  va_list ap;

  if (n_changes == -1)
    {
      g_assert_false (cs->fired);
      return;
    }

  g_assert_true (cs->fired);
  g_assert_true (cs->changed);

  sz = g_hash_table_size (cs->changes);
  g_assert_cmpint (sz, ==, n_changes);

  va_start (ap, n_changes);

  for (int i = 0; i < n_changes; i++)
    {
      const char *uuid = va_arg (ap, const char *);
      int op = va_arg (ap, int);
      char *key;

      g_assert_nonnull (uuid);
      key = g_hash_table_lookup (cs->changes, uuid);

      g_assert_nonnull (key);
      g_assert_cmpint (GPOINTER_TO_INT (key), ==, op);
    }

  va_end (ap);
}

static void
on_bootacl_changed (BoltDomain *dom,
                    gboolean    changed,
                    GHashTable *changes,
                    gpointer    user_data)
{
  AclChangeSet *changeset = user_data;

  acl_change_set_clear (changeset);

  changeset->changed = changed;
  changeset->changes = g_hash_table_ref (changes);
  changeset->fired = TRUE;
}

static void
test_bootacl_update_udev (TestBootacl *tt, gconstpointer user)
{
  g_auto(AclChangeSet) changeset = ACL_CHANGE_SET_INIT;
  GStrv acl = tt->acl;
  BoltDomain *dom = tt->dom;
  guint slots = tt->slots;
  const char *syspath;
  guint n_free, n_used;
  guint n_signals = 0;

  syspath = mock_sysfs_domain_get_syspath (tt->sysfs, tt->dom_sysid);

  /* fill in some uuids via sysfs */
  g_signal_connect (dom, "notify::bootacl",
                    G_CALLBACK (on_bootacl_notify),
                    &n_signals);

  g_signal_connect (dom, "bootacl-changed",
                    G_CALLBACK (on_bootacl_changed),
                    &changeset);

  for (guint i = 0; i < 8; i++)
    {
      g_autoptr(udev_device) ud = NULL;
      GStrv have = NULL;
      g_autofree const char **used = NULL;
      char *uuid = NULL;
      struct udev *udev;
      guint n;

      uuid = g_strdup_printf ("deadbab%x-0200-0100-ffff-ffffffffffff", i);
      bolt_set_str (&acl[i], uuid);

      test_bootacl_write_acl (tt, acl);

      udev = udev_new ();
      ud = udev_device_new_from_syspath (udev, syspath);
      bolt_domain_update_from_udev (dom, ud);

      g_assert_cmpuint (n_signals, ==, i + 1);
      g_assert_true (bolt_domain_bootacl_contains (dom, acl[i]));

      n = bolt_domain_bootacl_slots (dom, &n_free);
      g_assert_cmpuint (n, ==, slots);
      g_assert_cmpuint (n_free, ==, slots - (i + 1));

      used = bolt_domain_bootacl_get_used (dom, &n_used);
      g_assert_cmpuint (n_used, ==, i + 1);
      g_assert_nonnull (used);

      have = bolt_domain_get_bootacl (dom);
      bolt_assert_strv_equal (have, acl, -1);

      /* we verify the bootacl-changed signal */
      acl_change_set_verify (&changeset, 1, uuid, '+');
      changeset.fired = FALSE;

      udev_unref (udev);
    }
}

static void
test_bootacl_update_online (TestBootacl *tt, gconstpointer user)
{
  g_autoptr(GError) err = NULL;
  g_auto(AclChangeSet) changeset = ACL_CHANGE_SET_INIT;
  g_auto(GStrv) sysacl = NULL;
  GStrv acl = tt->acl;
  BoltDomain *dom = tt->dom;
  gboolean ok;
  guint slots;

  g_signal_connect (dom, "bootacl-changed",
                    G_CALLBACK (on_bootacl_changed),
                    &changeset);

  for (guint i = 0; i < tt->slots; i++)
    {
      test_bootacl_add_uuid (tt, dom, i, "deadbab%x-0200-0100-ffff-ffffffffffff", i);
      test_bootacl_read_acl (tt, &sysacl);

      g_assert_nonnull (bolt_strv_contains (sysacl, acl[i]));

      acl_change_set_verify (&changeset, 1, acl[i], '+');
      changeset.fired = FALSE;

      bolt_assert_strv_equal (acl, sysacl, -1);
    }

  /* verify with what we have in mock sysfs */
  test_bootacl_read_acl (tt, &sysacl);
  slots = bolt_strv_length (acl);
  dump_strv (sysacl, "sysacl ");

  /* NB: acl was verified to be in sync with domain's acl */
  bolt_assert_strv_equal (acl, sysacl, -1);

  /* lets overwrite all the bootacl entries bit by bit
   *  and also verify we honor FIFO when replacing them
   */
  for (guint i = 0; i < tt->slots; i++)
    {
      g_auto(GStrv) have = NULL;

      /* NB: different uuid pattern from above (0200-0100) */
      test_bootacl_add_uuid (tt, dom, i, "deadbab%x-0200-aaaa-ffff-ffffffffffff", i);
      test_bootacl_read_acl (tt, &have);

      g_assert_nonnull (bolt_strv_contains (have, acl[i]));
      g_assert_cmpstr (have[slots - 1], ==, acl[i]);

      /* check the bootacl-changed signal emission:
       *  add for the new one, remove for the overwritten one
       */
      acl_change_set_verify (&changeset, 2, acl[i], '+', sysacl[i], '-');
      changeset.fired = FALSE;
    }

  /* remove all the entries */
  test_bootacl_read_acl (tt, &sysacl);
  slots = bolt_strv_length (acl);

  dump_strv (sysacl, "sysacl ");


  for (guint i = 0; i < slots; i++)
    {
      g_auto(GStrv) have = NULL;
      char *uuid = sysacl[i];

      changeset.fired = FALSE;

      test_bootacl_del_uuid (tt, dom, uuid);
      test_bootacl_read_acl (tt, &have);

      g_assert_null (bolt_strv_contains (have, uuid));

      /* check the bootacl-changed signal emission */
      acl_change_set_verify (&changeset, 1, uuid, '-');
      changeset.fired = FALSE;
    }

  /* now we set a bunch in one-go */
  for (guint i = 0; i < tt->slots; i++)
    bolt_set_strdup_printf (&acl[i], "deadbab%x-cccc-0100-ffff-ffffffffffff", i);

  changeset.fired = FALSE;
  ok = bolt_domain_bootacl_set (dom, acl, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  /* check we got removed signals for all of them */
  g_assert_true (changeset.fired);
  g_assert_true (changeset.changed);
  g_assert_cmpuint (g_hash_table_size (changeset.changes), ==, slots);
  changeset.fired = FALSE;

  dump_strv (sysacl, "sysacl ");
  test_bootacl_read_acl (tt, &sysacl);
  bolt_assert_strv_equal (acl, sysacl, -1);

  /* check that if we set the same bootacl as
   * we already have, we get FALSE but no error */
  changeset.fired = FALSE;
  ok = bolt_domain_bootacl_set (dom, acl, &err);
  g_assert_no_error (err);
  g_assert_false (ok);
  g_assert_false (changeset.fired);
}

static void
test_bootacl_update_offline (TestBootacl *tt, gconstpointer user)
{
  g_autoptr(GError) err = NULL;
  g_autoptr(BoltStore) store = NULL;
  g_auto(GStrv) sysacl = NULL;
  g_auto(BoltTmpDir) dir = NULL;
  BoltDomain *dom = tt->dom;
  gboolean ok;
  GStrv have;
  GStrv acl = tt->acl;
  guint k;

  dir = bolt_tmp_dir_make ("bolt.sysfs.XXXXXX", NULL);
  store = bolt_store_new (dir);

  ok = bolt_store_put_domain (store, dom, &err);
  g_assert_no_error (err);
  g_assert_true (ok);
  g_assert_true (bolt_domain_is_stored (dom));
  g_assert_true (bolt_domain_supports_bootacl (dom));

  /* 1. disconnect and add uuids that will get added to the journal */
  g_debug ("1. adding uuids offline");
  bolt_domain_disconnected (dom);

  for (guint i = 0; i < tt->slots / 2; i++)
    test_bootacl_add_uuid (tt, dom, i, "deadbab%x-0200-0100-ffff-ffffffffffff", i);

  have = bolt_domain_get_bootacl (dom);
  bolt_assert_strv_equal (have, acl, -1);

  /*   connect, and make sure we have sync */
  test_bootacl_connect_and_verify (tt, dom, &sysacl);

  /* 2. disconnect and remove uuids so they will end up in the journal */
  /*    remove the first quarter of uuids */
  g_debug ("2. remove uuids offline");
  bolt_domain_disconnected (dom);

  for (guint i = 0; i < tt->slots / 4; i++)
    test_bootacl_del_uuid (tt, dom, acl[i]);

  /*    simulate external changes: uuids added at the end */
  for (guint i = tt->slots / 2 + 1; i < tt->slots; i++)
    {
      bolt_set_strdup_printf (&sysacl[i], "deadbab%x-0200-0100-ffff-ffffffffffff", i);
      bolt_set_strdup_printf (&acl[i], "deadbab%x-0200-0100-ffff-ffffffffffff", i);
    }
  /*    write the external modifications */
  test_bootacl_write_acl (tt, sysacl);

  /*   connect, and make sure we have sync */
  test_bootacl_connect_and_verify (tt, dom, &sysacl);

  /* 3. simulate external modifications on top of journaled changes */
  g_debug ("3. external updates and offline changes");

  bolt_domain_disconnected (dom);
  dump_strv (bolt_domain_get_bootacl (dom), "domain ");

  /*    current state: [0,  N/4]: empty
   *                   [N/4,  N]: filled */
  test_bootacl_read_acl (tt, &sysacl);

  /*    [ 0 ] externally added and added in the journal (duplicated) */
  k = 0;
  bolt_set_strdup_printf (&sysacl[k], "deadbab%x-0200-0100-ffff-ffffffffffff", k);
  test_bootacl_add_uuid (tt, dom, k, "deadbab%x-0200-0100-ffff-ffffffffffff", k);

  /*    [ 1 ] added via the journal */
  k = 1;
  test_bootacl_add_uuid (tt, dom, k, "deadbab%x-0200-0100-ffff-ffffffffffff", k);
  bolt_set_strdup_printf (&acl[k], "deadbab%x-0200-0100-ffff-ffffffffffff", k);

  /*    [N/2+1] removed externally and via the journal */
  k = tt->slots / 2 + 1;
  bolt_set_strdup (&sysacl[k], "");
  test_bootacl_del_uuid (tt, dom, acl[k]);

  /*    [N/2+2] removed via the journal */
  k = tt->slots / 2 + 2;
  test_bootacl_del_uuid (tt, dom, acl[k]);

  /*    write the external modifications */
  test_bootacl_write_acl (tt, sysacl);

  /*   connect, and make sure we have sync */
  test_bootacl_connect_and_verify (tt, dom, &sysacl);

  /* 4. we pretend we got disconnected and reconnected with no change */
  g_debug ("4. no change reconnect");
  bolt_domain_disconnected (dom);
  test_bootacl_read_acl (tt, &sysacl);
}

static gboolean
bootacl_allocator (BoltDomain *domain,
                   GStrv       bootacl,
                   const char *uid,
                   gint       *slot,
                   gpointer    data)
{
  g_assert_nonnull (bootacl);
  g_assert_nonnull (uid);
  g_assert_nonnull (slot);
  g_assert_cmpint (*slot, >, -1);

  *slot = 0;
  return TRUE;
}

static void
test_bootacl_allocate (TestBootacl *tt, gconstpointer user)
{
  GStrv acl = tt->acl;
  BoltDomain *dom = tt->dom;

  g_signal_connect (dom, "bootacl-alloc",
                    G_CALLBACK (bootacl_allocator),
                    NULL);

  for (guint i = 0; i < tt->slots; i++)
    {
      g_auto(GStrv) have = NULL;
      GStrv domacl;

      test_bootacl_add_uuid (tt, dom, 0, "deadbab%x-0200-0100-ffff-ffffffffffff", i);
      test_bootacl_read_acl (tt, &have);

      g_assert_cmpstr (have[0], ==, acl[0]);
      bolt_assert_strv_equal (acl, have, -1);

      domacl = bolt_domain_get_bootacl (dom);
      bolt_assert_strv_equal (domacl, have, -1);
    }
}

static void
test_check_kernel_version (TestSysfs *tt, gconstpointer user)
{
  gboolean ok;

  /* simulate read errors */
  ok = mock_sysfs_set_osrelease (tt->sysfs, NULL);
  g_assert_true (ok);
  g_assert_false (bolt_check_kernel_version (1, 0));

  /* short kernel version */
  ok = mock_sysfs_set_osrelease (tt->sysfs, "1.0");
  g_assert_true (ok);
  g_assert_true (bolt_check_kernel_version (1, 0));
  g_assert_false (bolt_check_kernel_version (1, 1));
  g_assert_false (bolt_check_kernel_version (2, 0));

  /* more realistic kernel version */
  ok = mock_sysfs_set_osrelease (tt->sysfs, "1.0.0-111.fc1");
  g_assert_true (ok);
  g_assert_true (bolt_check_kernel_version (1, 0));
  g_assert_false (bolt_check_kernel_version (1, 1));
  g_assert_false (bolt_check_kernel_version (2, 0));
}

int
main (int argc, char **argv)
{

  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  bolt_dbus_ensure_resources ();

  g_test_add ("/sysfs/domain_for_device",
              TestSysfs,
              NULL,
              test_sysfs_setup,
              test_sysfs_domain_for_device,
              test_sysfs_tear_down);

  g_test_add ("/sysfs/info_for_device",
              TestSysfs,
              NULL,
              test_sysfs_setup,
              test_sysfs_info_for_device,
              test_sysfs_tear_down);

  g_test_add ("/sysfs/read_iommu",
              TestSysfs,
              NULL,
              test_sysfs_setup,
              test_sysfs_read_iommu,
              test_sysfs_tear_down);

  g_test_add ("/sysfs/domain/basic",
              TestSysfs,
              NULL,
              test_sysfs_setup,
              test_sysfs_domains,
              test_sysfs_tear_down);

  g_test_add ("/sysfs/domain/connect",
              TestSysfs,
              NULL,
              test_sysfs_setup,
              test_sysfs_domain_connect,
              test_sysfs_tear_down);

  g_test_add ("/sysfs/domain/bootacl/basic",
              TestBootacl,
              NULL,
              test_bootacl_setup,
              test_bootacl_basic,
              test_bootacl_tear_down);

  g_test_add ("/sysfs/domain/bootacl/errors",
              TestBootacl,
              NULL,
              test_bootacl_setup,
              test_bootacl_errors,
              test_bootacl_tear_down);

  g_test_add ("/sysfs/domain/bootacl/update_udev",
              TestBootacl,
              NULL,
              test_bootacl_setup,
              test_bootacl_update_udev,
              test_bootacl_tear_down);

  g_test_add ("/sysfs/domain/bootacl/update_online",
              TestBootacl,
              NULL,
              test_bootacl_setup,
              test_bootacl_update_online,
              test_bootacl_tear_down);

  g_test_add ("/sysfs/domain/bootacl/update_offline",
              TestBootacl,
              NULL,
              test_bootacl_setup,
              test_bootacl_update_offline,
              test_bootacl_tear_down);

  g_test_add ("/sysfs/domain/bootacl/allocate",
              TestBootacl,
              NULL,
              test_bootacl_setup,
              test_bootacl_allocate,
              test_bootacl_tear_down);

  g_test_add ("/self/check-kernel-version",
              TestSysfs,
              NULL,
              test_sysfs_setup,
              test_check_kernel_version,
              test_sysfs_tear_down);

  return g_test_run ();
}

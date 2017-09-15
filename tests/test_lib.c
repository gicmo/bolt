/*
 * Copyright © 2017 Red Hat, Inc
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


#include <glib.h>
#include <glib/gprintf.h>

#include <errno.h>
#include <unistd.h>

#include <umockdev.h>

#include "ioutils.h"
#include "manager.h"
#include "store.h"

#include <locale.h>

#define MAX_PATH_LEN 4096

static void
cleanup_dir (DIR *d)
{
  struct dirent *de = NULL;

  for (errno = 0, de = readdir (d); de != NULL; errno = 0, de = readdir (d))
    {
      g_autoptr(GError) error = NULL;
      int uflag               = 0;

      if (!g_strcmp0 (de->d_name, ".") || !g_strcmp0 (de->d_name, ".."))
        continue;

      if (de->d_type == DT_DIR)
        {
          g_autoptr(DIR) cd = NULL;
          cd                = tb_opendirat (d, de->d_name, O_RDONLY, &error);
          if (cd == NULL)
            continue;

          cleanup_dir (cd);
          uflag = AT_REMOVEDIR;
        }

      unlinkat (dirfd (d), de->d_name, uflag);
    }
}

typedef struct StoreTest
{
  TbStore *store;
} StoreTest;

typedef struct Params
{
  const char *path;
} Params;

static void
store_test_set_up (StoreTest *tt, gconstpointer user_data)
{
  const Params *p = user_data;

  tt->store = tb_store_new (p->path);
}

static void
store_test_tear_down (StoreTest *tt, gconstpointer user_data)
{
  const Params *p         = user_data;

  g_autoptr(DIR) d        = NULL;
  g_autoptr(GError) error = NULL;
  int res;

  g_object_unref (tt->store);

  d = tb_opendir (p->path, &error);

  if (d)
    {
      g_debug ("Cleaning up: %s", p->path);
      cleanup_dir (d);
    }
  else
    {
      g_warning ("Cleanup failed for %s: %s", p->path, error->message);
    }

  res = rmdir (p->path);
  if (res != 0)
    g_warning ("Cleanup failed for %s: %s", p->path, g_strerror (errno));
}

static void
test_store_basic (StoreTest *tt, gconstpointer user_data)
{
  //  const Params *p = user_data;
  g_autoptr(TbDevice) stored = NULL;
  g_autoptr(TbDevice) merged = NULL;
  g_autoptr(GError) err = NULL;
  g_autoptr(TbDevice) dev = NULL;
  g_autoptr(GFile) key = NULL;
  char data[TB_KEY_CHARS] = { 0, };
  g_autofree char *uuid = NULL;
  ssize_t n = 0;
  int fd = -1;
  gboolean ok;

  uuid = g_uuid_string_random ();

  g_assert_false (tb_store_have (tt->store, uuid));
  g_assert_false (tb_store_have_key (tt->store, uuid));

  dev = g_object_new (TB_TYPE_DEVICE,
                      "uid", uuid,
                      "device-name", "Blitz",
                      "device-id", 0x33,
                      "vendor-name", "GNOME",
                      "vendor-id", 0x23,
                      NULL);

  g_debug ("Storing device: %s", uuid);
  ok = tb_store_put (tt->store, dev, &err);
  g_assert_no_error (err);
  g_assert_true (ok);
  g_assert_true (tb_store_have (tt->store, uuid));

  g_object_set (dev, "policy", TB_POLICY_AUTO, NULL);
  g_assert_cmpint (tb_device_get_policy (dev), ==, TB_POLICY_AUTO);

  ok = tb_store_put (tt->store, dev, &err);
  g_assert_true (ok);
  g_assert_no_error (err);

  g_debug ("Generating key");
  ok = tb_store_create_key (tt->store, dev, &err);
  g_assert_no_error (err);
  g_assert_true (ok);
  g_assert_true (tb_store_have_key (tt->store, uuid));
  fd = tb_store_open_key (tt->store, uuid, &err);
  g_assert_no_error (err);
  g_assert_cmpint (fd, >, -1);

  n = tb_read_all (fd, data, TB_KEY_CHARS, &err);
  g_assert_no_error (err);
  g_assert_true (n == TB_KEY_CHARS);

  g_debug ("Key: [%li] %s", n, data);

  stored = tb_store_get (tt->store, uuid, &err);
  g_assert_no_error (err);
  g_assert_nonnull (stored);

  g_assert_cmpstr (tb_device_get_uid (dev), ==, tb_device_get_uid (stored));
  g_assert_cmpstr (tb_device_get_name (dev), ==, tb_device_get_name (stored));
  g_assert_cmpuint (tb_device_get_device_id (dev), ==, tb_device_get_device_id (stored));
  g_assert_cmpstr (tb_device_get_vendor_name (dev), ==, tb_device_get_vendor_name (stored));
  g_assert_cmpuint (tb_device_get_vendor_id (dev), ==, tb_device_get_vendor_id (stored));
  g_assert_cmpint (tb_device_get_policy (dev), ==, tb_device_get_policy (stored));

  merged = g_object_new (TB_TYPE_DEVICE,
                         "uid", uuid,
                         "device-name", "Blitz",
                         "vendor-name", "GNOME",
                         NULL);

  ok = tb_store_merge (tt->store, merged, &err);
  g_assert_true (ok);
  g_assert_no_error (err);

  g_assert_true (tb_device_in_store (merged));
  g_assert_cmpint (tb_device_get_policy (merged), ==, TB_POLICY_AUTO);

  ok = tb_store_delete (tt->store, uuid, &err);
  g_assert_true (ok);
  g_assert_no_error (err);
}

typedef struct ManagerTest
{
  TbManager       *mgr;
  UMockdevTestbed *bed;
} ManagerTest;

static void
manager_test_set_up (ManagerTest *tt, gconstpointer user_data)
{
  g_autoptr(GError) error = NULL;
  const Params *p         = user_data;

  tt->bed = umockdev_testbed_new ();
  tt->mgr = g_object_new (TB_TYPE_MANAGER, "db", p->path, NULL);

  g_assert_no_error (error);
  g_assert_nonnull (tt->mgr);
}

static void
manager_test_tear_down (ManagerTest *tt, gconstpointer user_data)
{
  g_object_unref (tt->mgr);
  g_object_unref (tt->bed);
}

static char *
udev_mock_add_domain (UMockdevTestbed *bed, int id, TbSecurity level)
{
  g_autofree char *security = NULL;
  char name[256]            = {
    0,
  };
  char *path;

  g_snprintf (name, sizeof (name), "domain%d", id);
  security = tb_security_to_string (level);

  path = umockdev_testbed_add_device (bed, "thunderbolt",
                                      name,
                                      NULL,
                                      "security", security,
                                      NULL,
                                      "DEVTYPE", "thunderbolt_domain",
                                      NULL);

  g_assert_nonnull (path);
  return path;
}

static void
udev_mock_add_device (UMockdevTestbed *bed, const char *parent, const char *id, TbDevice *dev)
{
  const char *uid         = tb_device_get_uid (dev);
  const char *device_name = tb_device_get_name (dev);
  const guint device_id   = tb_device_get_device_id (dev);
  const guint vendor_id   = tb_device_get_vendor_id (dev);
  const char *vendor_name = tb_device_get_vendor_name (dev);
  TbAuthLevel auth             = tb_device_get_authorized (dev);
  char authorized[16]     = {
    0,
  };
  g_autofree char *vendor_idstr = NULL;
  g_autofree char *device_idstr = NULL;
  g_autofree char *path         = NULL;

  g_snprintf (authorized, sizeof (authorized), "%d", (int) auth);
  vendor_idstr = g_strdup_printf ("%u", vendor_id);
  device_idstr = g_strdup_printf ("%u", device_id);

  path = umockdev_testbed_add_device (bed, "thunderbolt",
                                      id, parent,
                                      /* attributes */
                                      "device", device_idstr,
                                      "device_name", device_name,
                                      "vendor", vendor_idstr,
                                      "vendor_name", vendor_name,
                                      "authorized", authorized,
                                      "unique_id", uid,
                                      NULL,
                                      /* properties */
                                      "DEVTYPE", "thunderbolt_device",
                                      NULL);

  g_assert_nonnull (path);
  g_object_set (dev, "sysfs", path, NULL);
}

static TbDevice *
udev_mock_add_new_device (UMockdevTestbed *bed,
                          const char      *parent,
                          const char      *id,
                          const char      *uuid,
                          const char      *device_name,
                          guint            device_id,
                          TbAuthLevel      auth)
{
  TbDevice *dev              = NULL;
  g_autofree char *generated = NULL;

  if (uuid == NULL)
    {
      generated = g_uuid_string_random ();
      uuid      = generated;
    }

  dev = g_object_new (TB_TYPE_DEVICE,
                      "uid", uuid,
                      "device-id", device_id,
                      "device-name", device_name,
                      "vendor-id", 0x23,
                      "vendor-name", "GNOME.Org",
                      "authorized", auth,
                      NULL);

  udev_mock_add_device (bed, parent, id, dev);

  return dev;
}

static void
udev_mock_set_authorized (UMockdevTestbed *bed, TbDevice *dev, TbAuthLevel auth)
{
  const char *path = tb_device_get_sysfs_path (dev);

  umockdev_testbed_set_attribute_int (bed, path, "authorized", (int) auth);
  umockdev_testbed_uevent (bed, path, "change");
}

static void
manager_test_basic (ManagerTest *tt, gconstpointer user_data)
{
  g_autofree char *domain   = NULL;

  g_autoptr(TbDevice) host  = NULL;
  g_autoptr(TbDevice) cable = NULL;
  g_autoptr(GError) error   = NULL;
  const GPtrArray *devs     = NULL;
  gboolean ok;

  domain = udev_mock_add_domain (tt->bed, 0, TB_SECURITY_SECURE);
  host   = udev_mock_add_new_device (tt->bed,
                                     domain, "0-0",
                                     NULL,
                                     "Laptop", 0x23,
                                     TB_AUTH_LEVEL_UNAUTHORIZED);

  cable = udev_mock_add_new_device (tt->bed,
                                    tb_device_get_sysfs_path (host),
                                    "0-1",
                                    NULL,
                                    "TB Cable",
                                    0x24,
                                    TB_AUTH_LEVEL_UNAUTHORIZED);

  g_debug (" domain:   %s", domain);
  g_debug ("  host:    %s", tb_device_get_sysfs_path (host));
  g_debug ("   cable:  %s", tb_device_get_sysfs_path (cable));

  ok = g_initable_init (G_INITABLE (tt->mgr), NULL, &error);
  g_assert_no_error (error);
  g_assert_true (ok);

  devs = tb_manager_list_attached (tt->mgr);

  /* we should have the cable and the host */
  g_assert_cmpuint (devs->len, ==, 2);
}

static gboolean
on_timeout (gpointer user_data)
{
  GMainLoop *mainloop = (GMainLoop *) user_data;

  g_main_loop_quit (mainloop);
  return FALSE;
}

static void
manager_test_monitor (ManagerTest *tt, gconstpointer user_data)
{
  g_autoptr(GMainLoop) mainloop = NULL;
  g_autoptr(TbDevice) cable = NULL;
  g_autoptr(TbDevice) host = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *domain = NULL;
  const GPtrArray *devs = NULL;
  TbDevice *d = NULL;

  gboolean ok;

  ok = g_initable_init (G_INITABLE (tt->mgr), NULL, &error);
  g_assert_no_error (error);
  g_assert_true (ok);

  devs = tb_manager_list_attached (tt->mgr);
  g_assert_cmpuint (devs->len, ==, 0);

  /* lets add devices */
  domain = udev_mock_add_domain (tt->bed, 0, TB_SECURITY_SECURE);

  /* add the host */
  host = udev_mock_add_new_device (tt->bed,
                                   domain,
                                   "0-0",
                                   NULL,
                                   "Laptop", 0x23,
                                   TB_AUTH_LEVEL_UNAUTHORIZED);

  mainloop = g_main_loop_new (NULL, FALSE);
  g_timeout_add (500, on_timeout, mainloop);
  g_main_loop_run (mainloop);

  devs = tb_manager_list_attached (tt->mgr);
  g_assert_cmpuint (devs->len, ==, 1);
  d = tb_manager_lookup (tt->mgr, tb_device_get_uid (host));
  g_assert_nonnull (d);
  g_clear_object (&d);

  g_debug (" got the host: %s", tb_device_get_sysfs_path (host));

  /* add the cable */
  cable = udev_mock_add_new_device (tt->bed,
                                    tb_device_get_sysfs_path (host),
                                    "0-1",
                                    NULL,
                                    "TB Cable", 0x24,
                                    TB_AUTH_LEVEL_UNAUTHORIZED);

  g_timeout_add (500, on_timeout, mainloop);
  g_main_loop_run (mainloop);

  /* simulate that the cable got authorized externally */
  devs = tb_manager_list_attached (tt->mgr);
  g_assert_cmpuint (devs->len, ==, 2);
  d = tb_manager_lookup (tt->mgr, tb_device_get_uid (cable));
  g_assert_nonnull (d);

  g_debug (" got the cable: %s", tb_device_get_sysfs_path (cable));

  udev_mock_set_authorized (tt->bed, cable, TB_AUTH_LEVEL_AUTHORIZED);
  g_timeout_add (500, on_timeout, mainloop);
  g_main_loop_run (mainloop);

  g_assert_cmpint (tb_device_get_authorized (d), ==, TB_AUTH_LEVEL_AUTHORIZED);

  umockdev_testbed_uevent (tt->bed, tb_device_get_sysfs_path (cable), "remove");
  umockdev_testbed_remove_device (tt->bed, tb_device_get_sysfs_path (cable));
  g_timeout_add (500, on_timeout, mainloop);
  g_main_loop_run (mainloop);

  devs = tb_manager_list_attached (tt->mgr);
  g_assert_cmpuint (devs->len, ==, 1);

  g_assert_cmpint (tb_device_get_authorized (d), ==, TB_AUTH_LEVEL_UNKNOWN);
  g_assert_null (tb_device_get_sysfs_path (d));

  g_clear_object (&d);
}

int
main (int argc, char **argv)
{
  Params p = {
    NULL,
  };
  const char *tmp = NULL;
  char buffer[]   = "tb.XXXXXX";

  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  p.path = tmp = g_mkdtemp_full (buffer, 0755);
  ;

  g_debug ("library dir: %s", tmp);

  g_test_add ("/store/basic",
              StoreTest,
              &p,
              store_test_set_up,
              test_store_basic,
              store_test_tear_down);

  if (umockdev_in_mock_environment ())
    {
      g_test_add ("/manager/basic",
                  ManagerTest,
                  &p,
                  manager_test_set_up,
                  manager_test_basic,
                  manager_test_tear_down);

      g_test_add ("/manager/monitor",
                  ManagerTest,
                  &p,
                  manager_test_set_up,
                  manager_test_monitor,
                  manager_test_tear_down);
    }

  return g_test_run ();
}

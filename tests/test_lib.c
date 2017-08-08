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

#include <umockdev.h>

#include "ioutils.h"
#include "manager.h"
#include "store.h"

#include <locale.h>

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
  g_object_unref (tt->store);
}

static void
test_store_basic (StoreTest *tt, gconstpointer user_data)
{
  //  const Params *p = user_data;
  g_autoptr(GError) err      = NULL;
  g_autoptr(TbDevice) dev    = NULL;
  g_autoptr(TbDevice) stored = NULL;
  g_autoptr(TbDevice) merged = NULL;
  g_autoptr(GFile) key       = NULL;
  char data[TB_KEY_CHARS]    = {
    0,
  };
  g_autofree char *uuid = NULL;
  ssize_t n             = 0;
  int fd                = -1;
  gboolean ok;

  uuid = g_uuid_string_random ();

  g_assert_false (tb_store_have (tt->store, uuid));
  g_assert_false (tb_store_have_key (tt->store, uuid));

  dev = g_object_new (TB_TYPE_DEVICE,
                      "uid",
                      uuid,
                      "device-name",
                      "Blitz",
                      "device-id",
                      0x33,
                      "vendor-name",
                      "GNOME",
                      "vendor-id",
                      0x23,
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

  merged = g_object_new (TB_TYPE_DEVICE, "uid", uuid, "device-name", "Blitz", "vendor-name", "GNOME", NULL);

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

  path = umockdev_testbed_add_device (bed,
                                      "thunderbolt",
                                      name,
                                      NULL,
                                      "security",
                                      security,
                                      NULL,
                                      "DEVTYPE",
                                      "thunderbolt_domain",
                                      NULL);

  g_assert_nonnull (path);
  return path;
}

static char *
udev_mock_add_device (UMockdevTestbed *bed,
                      const char      *parent,
                      const char      *id,
                      const char      *uuid,
                      const char      *device_name,
                      const char      *device_id,
                      TbAuth           auth)
{
  g_autofree char *generated = NULL;
  char authorized[16]        = {
    0,
  };
  char *path;

  if (uuid == NULL)
    {
      generated = g_uuid_string_random ();
      uuid      = generated;
    }

  g_snprintf (authorized, sizeof (authorized), "%d", (int) auth);

  path = umockdev_testbed_add_device (bed,
                                      "thunderbolt",
                                      id,
                                      parent,
                                      "device_name",
                                      device_name,
                                      "device",
                                      device_id,
                                      "vendor",
                                      "042",
                                      "vendor_name",
                                      "GNOME.org",
                                      "authorized",
                                      authorized,
                                      "unique_id",
                                      uuid,
                                      NULL,
                                      "DEVTYPE",
                                      "thunderbolt_device",
                                      NULL);

  g_assert_nonnull (path);
  return path;
}

static void
manager_test_basic (ManagerTest *tt, gconstpointer user_data)
{
  g_autofree char *domain = NULL;
  g_autofree char *host   = NULL;
  g_autofree char *cable  = NULL;

  g_autoptr(GError) error = NULL;
  const GPtrArray *devs   = NULL;
  gboolean ok;

  domain = udev_mock_add_domain (tt->bed, 0, TB_SECURITY_SECURE);
  host   = udev_mock_add_device (tt->bed, domain, "0-0", NULL, "Laptop", "0x23", TB_AUTH_UNAUTHORIZED);

  cable = udev_mock_add_device (tt->bed, host, "0-1", NULL, "TB Cable", "0x24", TB_AUTH_UNAUTHORIZED);

  g_debug (" domain:   %s", domain);
  g_debug ("  host:    %s", host);
  g_debug ("   cable:  %s", cable);

  ok = g_initable_init (G_INITABLE (tt->mgr), NULL, &error);
  g_assert_no_error (error);
  g_assert_true (ok);

  devs = tb_manager_list_attached (tt->mgr);

  /* we should have the cable and the host */
  g_assert_cmpuint (devs->len, ==, 2);
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

  tmp    = g_mkdtemp_full (buffer, 0755);
  p.path = tmp;

  g_debug ("library dir: %s", tmp);

  g_test_add ("/store/basic", StoreTest, &p, store_test_set_up, test_store_basic, store_test_tear_down);

  if (umockdev_in_mock_environment ())
    {
      g_test_add ("/manager/basic",
                  ManagerTest,
                  &p,
                  manager_test_set_up,
                  manager_test_basic,
                  manager_test_tear_down);
    }

  return g_test_run ();
}

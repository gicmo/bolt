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

#include "config.h"

#include "bolt-error.h"
#include "bolt-io.h"
#include "bolt-store.h"

#include <glib.h>
#include <gio/gio.h>
#include <glib/gprintf.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h> /* unlinkat */

static void
cleanup_dir (DIR *d)
{
  struct dirent *de = NULL;

  for (errno = 0, de = readdir (d); de != NULL; errno = 0, de = readdir (d))
    {
      g_autoptr(GError) error = NULL;
      int uflag               = 0;

      if (!g_strcmp0 (de->d_name, ".") ||
          !g_strcmp0 (de->d_name, ".."))
        continue;

      if (de->d_type == DT_DIR)
        {
          g_autoptr(DIR) cd = NULL;

          cd = bolt_opendir_at (dirfd (d), de->d_name, O_RDONLY, &error);
          if (cd == NULL)
            continue;

          cleanup_dir (cd);
          uflag = AT_REMOVEDIR;
        }

      bolt_unlink_at (dirfd (d), de->d_name, uflag, NULL);
    }
}


typedef struct
{
  const char *path;
  BoltStore  *store;
} TestStore;


static void
test_store_setup (TestStore *tt, gconstpointer user_data)
{
  g_autoptr(GError) error = NULL;

  tt->path = g_dir_make_tmp ("bolt.auth.XXXXXX",
                             &error);

  if (tt->path == NULL)
    {
      g_critical ("Could not create tmp dir: %s",
                  error->message);
      return;
    }


  tt->store = bolt_store_new (tt->path);
  if (tt->store == NULL)
    {
      g_critical ("Could not create store at %s",
                  tt->path);
      return;
    }

  g_debug ("store at '%s'", tt->path);

}

static void
test_store_tear_down (TestStore *tt, gconstpointer user_data)
{
  DIR *d = NULL;

  g_autoptr(GError) error = NULL;

  if (tt->store)
    g_clear_object (&tt->store);

  d = bolt_opendir (tt->path, &error);
  if (d)
    {
      g_debug ("Cleaning up: %s", tt->path);
      cleanup_dir (d);
      bolt_closedir (d, NULL);
      bolt_rmdir (tt->path, &error);
    }

  if (error != NULL)
    g_warning ("Could not clean up dir: %s", error->message);
}

static void
test_store_basic (TestStore *tt, gconstpointer user_data)
{
  g_autoptr(BoltDevice) dev = NULL;
  g_autoptr(BoltDevice) stored = NULL;
  g_autoptr(BoltKey) key = NULL;
  g_autoptr(GError) error = NULL;
  char uid[] = "fbc83890-e9bf-45e5-a777-b3728490989c";
  BoltKeyState keystate;
  gboolean ok;


  dev = g_object_new (BOLT_TYPE_DEVICE,
                      "uid", uid,
                      "name", "Laptop",
                      "vendor", "GNOME.org",
                      "status", BOLT_STATUS_DISCONNECTED,
                      NULL);

  stored = bolt_store_get_device (tt->store, uid, &error);
  g_assert_nonnull (error);
  g_assert_true (bolt_err_notfound (error));
  g_assert_null (stored);
  g_clear_error (&error);

  ok = bolt_store_put_device (tt->store, dev, BOLT_POLICY_AUTO, NULL, &error);
  g_assert_no_error (error);
  g_assert_true (ok);

  stored = bolt_store_get_device (tt->store, uid, &error);
  g_assert_no_error (error);
  g_assert_nonnull (stored);

  keystate = bolt_store_have_key (tt->store, uid);
  g_assert_cmpuint (keystate, ==, 0);

  g_assert_cmpstr (bolt_device_get_uid (stored), ==, bolt_device_get_uid (dev));
  g_assert_cmpstr (bolt_device_get_name (stored), ==, bolt_device_get_name (dev));
  g_assert_cmpstr (bolt_device_get_vendor (stored), ==, bolt_device_get_vendor (dev));

  g_assert_cmpuint (bolt_device_get_policy (stored), ==, BOLT_POLICY_AUTO);
  g_assert_cmpuint (bolt_device_get_stored (stored), ==, TRUE);
  g_assert_cmpuint (bolt_device_get_keystate (stored), ==, BOLT_KEY_MISSING);

  g_clear_object (&stored);
  g_clear_object (&dev);

  uid[0] = 'a';
  dev = g_object_new (BOLT_TYPE_DEVICE,
                      "uid", uid,
                      "name", "Laptop",
                      "vendor", "GNOME.org",
                      "status", BOLT_STATUS_DISCONNECTED,
                      NULL);

  stored = bolt_store_get_device (tt->store, uid, &error);
  g_assert_nonnull (error);
  g_assert_true (bolt_err_notfound (error));
  g_assert_null (stored);
  g_clear_error (&error);
  g_assert_no_error (error);

  key = bolt_key_new ();
  g_assert_no_error (error);
  g_assert_true (ok);

  ok = bolt_store_put_device (tt->store, dev, BOLT_POLICY_MANUAL, key, &error);
  g_assert_no_error (error);
  g_assert_true (ok);

  stored = bolt_store_get_device (tt->store, uid, &error);
  g_assert_no_error (error);
  g_assert_nonnull (stored);

  g_assert_cmpstr (bolt_device_get_uid (stored), ==, bolt_device_get_uid (dev));
  g_assert_cmpstr (bolt_device_get_name (stored), ==, bolt_device_get_name (dev));
  g_assert_cmpstr (bolt_device_get_vendor (stored), ==, bolt_device_get_vendor (dev));

  g_assert_cmpuint (bolt_device_get_policy (stored), ==, BOLT_POLICY_MANUAL);
  g_assert_cmpuint (bolt_device_get_stored (stored), ==, TRUE);
  g_assert_cmpuint (bolt_device_get_keystate (stored), ==, 1);

  keystate = bolt_store_have_key (tt->store, uid);
  g_assert_cmpuint (keystate, ==, 1);

  g_clear_object (&key);
  key = bolt_store_get_key (tt->store, uid, &error);
  g_assert_no_error (error);
  g_assert_nonnull (stored);

  /* ** deletion */

  /* non-existent */
  ok = bolt_store_del_device (tt->store, "transmogrifier", &error);
  g_assert_nonnull (error);
  g_assert_true (bolt_err_notfound (error));
  g_clear_error (&error);
  g_assert_no_error (error);

  ok = bolt_store_del_key (tt->store, "sesamoeffnedich", &error);
  g_assert_nonnull (error);
  g_assert_true (bolt_err_notfound (error));
  g_clear_error (&error);
  g_assert_no_error (error);

  /* remove existing device & key */
  ok = bolt_store_del_device (tt->store, uid, &error);
  g_assert_no_error (error);
  g_assert_true (ok);

  keystate = bolt_store_have_key (tt->store, uid);
  g_assert_cmpuint (keystate, !=, 0);

  ok = bolt_store_del_key (tt->store, uid, &error);
  g_assert_no_error (error);
  g_assert_true (ok);

  /* check that they are gone indeed */
  ok = bolt_store_del_device (tt->store, uid, &error);
  g_assert_nonnull (error);
  g_assert_true (bolt_err_notfound (error));
  g_clear_error (&error);
  g_assert_no_error (error);

  keystate = bolt_store_have_key (tt->store, uid);
  g_assert_cmpuint (keystate, ==, 0);

  ok = bolt_store_del_key (tt->store, uid, &error);
  g_assert_nonnull (error);
  g_assert_true (bolt_err_notfound (error));
  g_clear_error (&error);
  g_assert_no_error (error);

}


int
main (int argc, char **argv)
{

  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  g_test_add ("/daemon/store/basic",
              TestStore,
              NULL,
              test_store_setup,
              test_store_basic,
              test_store_tear_down);

  return g_test_run ();
}

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

#include "bolt-dbus.h"
#include "bolt-error.h"
#include "bolt-fs.h"
#include "bolt-io.h"
#include "bolt-str.h"
#include "bolt-test.h"

#include "bolt-config.h"
#include "bolt-store.h"

#include "mock-sysfs.h"

#include <glib.h>
#include <gio/gio.h>
#include <glib/gprintf.h>
#include <glib/gstdio.h>

#include <fcntl.h>
#include <locale.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h> /* unlinkat, truncate */

typedef struct
{
  char      *path;
  BoltStore *store;

} TestStore;

typedef struct
{
  GLogWriterFunc logger;
} TestContext;

static void
test_context_set_logger (TestContext *data, GLogWriterFunc logger)
{
  data->logger = logger;
}

static GLogWriterOutput
test_context_logger (GLogLevelFlags   log_level,
                     const GLogField *fields,
                     gsize            n_fields,
                     gpointer         user_data)
{
  const TestContext *data = user_data;

  return data->logger (log_level, fields, n_fields, NULL);
}

static GLogWriterOutput
null_logger (GLogLevelFlags   log_level,
             const GLogField *fields,
             gsize            n_fields,
             gpointer         user_data)
{
  return G_LOG_WRITER_HANDLED;
}

static void
test_store_setup (TestStore *tt, gconstpointer user_data)
{
  g_autoptr(GError) error = NULL;
  TestContext *ctx = (TestContext *) user_data;

  /* reset logger */
  test_context_set_logger (ctx, g_log_writer_default);

  ctx->logger = g_log_writer_default;

  tt->path = g_dir_make_tmp ("bolt.auth.XXXXXX",
                             &error);

  if (tt->path == NULL)
    {
      g_critical ("Could not create tmp dir: %s",
                  error->message);
      return;
    }


  tt->store = bolt_store_new (tt->path, &error);
  if (tt->store == NULL)
    {
      g_critical ("Could not create store at %s: %s",
                  tt->path, error->message);
      return;
    }

  g_debug ("store at '%s'", tt->path);
}

static void
test_store_tear_down (TestStore *tt, gconstpointer user_data)
{
  g_autoptr(GError) error = NULL;
  gboolean ok;

  g_clear_object (&tt->store);

  ok = bolt_fs_cleanup_dir (tt->path, &error);

  if (!ok)
    g_warning ("Could not clean up dir: %s", error->message);

  g_free (tt->path);
}

static void
test_store_basic (TestStore *tt, gconstpointer user_data)
{
  g_autoptr(BoltDevice) dev = NULL;
  g_autoptr(BoltDevice) stored = NULL;
  g_autoptr(BoltKey) key = NULL;
  g_autoptr(GFile) root = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *path = NULL;
  char uid[] = "fbc83890-e9bf-45e5-a777-b3728490989c";
  BoltKeyState keystate;
  guint version;
  gboolean ok;

  g_object_get (tt->store, "root", &root, NULL);
  path = g_file_get_path (root);
  g_assert_cmpstr (tt->path, ==, path);

  g_object_get (tt->store, "version", &version, NULL);
  g_assert_cmpuint (version, ==, BOLT_STORE_VERSION);

  version = bolt_store_get_version (tt->store);
  g_assert_cmpuint (version, ==, BOLT_STORE_VERSION);

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
  g_assert_cmpuint (bolt_device_get_generation (stored), ==, 0);

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
                      "generation", 4,
                      "status", BOLT_STATUS_DISCONNECTED,
                      NULL);

  stored = bolt_store_get_device (tt->store, uid, &error);
  g_assert_nonnull (error);
  g_assert_true (bolt_err_notfound (error));
  g_assert_null (stored);
  g_clear_error (&error);
  g_assert_no_error (error);

  key = bolt_key_new (NULL);
  g_assert_nonnull (key);

  ok = bolt_store_put_device (tt->store, dev, BOLT_POLICY_MANUAL, key, &error);
  g_assert_no_error (error);
  g_assert_true (ok);

  stored = bolt_store_get_device (tt->store, uid, &error);
  g_assert_no_error (error);
  g_assert_nonnull (stored);

  g_assert_cmpstr (bolt_device_get_uid (stored), ==, bolt_device_get_uid (dev));
  g_assert_cmpstr (bolt_device_get_name (stored), ==, bolt_device_get_name (dev));
  g_assert_cmpstr (bolt_device_get_vendor (stored), ==, bolt_device_get_vendor (dev));

  g_assert_cmpuint (bolt_device_get_generation (stored), ==, 4);
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
  g_assert_false (ok);
  g_clear_error (&error);
  g_assert_no_error (error);

  ok = bolt_store_del_key (tt->store, "sesamoeffnedich", &error);
  g_assert_nonnull (error);
  g_assert_true (bolt_err_notfound (error));
  g_assert_false (ok);
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
  g_assert_false (ok);
  g_clear_error (&error);
  g_assert_no_error (error);

  keystate = bolt_store_have_key (tt->store, uid);

  g_assert_cmpuint (keystate, ==, 0);

  ok = bolt_store_del_key (tt->store, uid, &error);
  g_assert_nonnull (error);
  g_assert_true (bolt_err_notfound (error));
  g_assert_false (ok);
  g_clear_error (&error);
  g_assert_no_error (error);

}

static void
test_store_update (TestStore *tt, gconstpointer user_data)
{
  g_autoptr(BoltDevice) dev = NULL;
  g_autoptr(BoltKey) key = NULL;
  g_autoptr(GError) err = NULL;
  char uid[] = "fbc83890-e9bf-45e5-a777-b3728490989c";
  BoltKeyState keystate;
  BoltPolicy policy;
  gboolean ok;
  guint64 storetime;

  policy = BOLT_POLICY_IOMMU;
  dev = g_object_new (BOLT_TYPE_DEVICE,
                      "uid", uid,
                      "name", "Laptop",
                      "vendor", "GNOME.org",
                      "status", BOLT_STATUS_DISCONNECTED,
                      "generation", 1,
                      NULL);

  key = bolt_key_new (NULL);
  g_assert_nonnull (key);

  ok = bolt_store_put_device (tt->store,
                              dev,
                              policy,
                              key,
                              &err);

  g_assert_no_error (err);
  g_assert_true (ok);

  keystate = bolt_device_get_keystate (dev);
  g_assert_cmpuint (keystate, ==, BOLT_KEY_NEW);
  g_assert_cmpuint (bolt_device_get_policy (dev), ==, policy);

  storetime = bolt_device_get_storetime (dev);

  g_object_set (G_OBJECT (dev),
                "generation", 3,
                "label", "My Laptop",
                NULL);

  /* update the device. generation and label should
   * change, but the rest should stay the same, esp.
   * keystate and also storetime should not change.
   * Also, BOLT_POLICY_DEFAULT should be ignored */
  ok = bolt_store_put_device (tt->store,
                              dev,
                              BOLT_POLICY_DEFAULT,
                              NULL,
                              &err);

  g_assert_no_error (err);
  g_assert_true (ok);

  keystate = bolt_device_get_keystate (dev);
  g_assert_cmpuint (keystate, ==, BOLT_KEY_NEW);
  g_assert_cmpuint (bolt_device_get_policy (dev), ==, policy);
  g_assert_cmpuint (bolt_device_get_storetime (dev), ==, storetime);
}

static void
test_store_config (TestStore *tt, gconstpointer user_data)
{
  g_autoptr(GKeyFile) kf = NULL;
  g_autoptr(GKeyFile) loaded = NULL;
  g_autoptr(GError) err = NULL;
  BoltAuthMode authmode;
  BoltPolicy policy;
  gboolean ok;
  BoltTri tri;

  kf = bolt_store_config_load (tt->store, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_assert_null (kf);
  g_clear_pointer (&err, g_error_free);

  kf = bolt_config_user_init ();
  g_assert_nonnull (kf);

  ok = bolt_store_config_save (tt->store, kf, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  loaded = bolt_store_config_load (tt->store, &err);
  g_assert_no_error (err);
  g_assert_nonnull (loaded);

  tri = bolt_config_load_default_policy (loaded, &policy, &err);
  g_assert_no_error (err);
  g_assert (tri == TRI_NO);

  /* */
  bolt_config_set_auth_mode (kf, "WRONG");
  ok = bolt_store_config_save (tt->store, kf, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  g_clear_pointer (&loaded, g_key_file_unref);
  loaded = bolt_store_config_load (tt->store, &err);
  g_assert_no_error (err);
  g_assert_nonnull (loaded);

  tri = bolt_config_load_auth_mode (loaded, &authmode, &err);
  g_assert_error (err, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS);
  g_assert (tri == TRI_ERROR);
  g_clear_pointer (&err, g_error_free);

  /*  */
  bolt_config_set_auth_mode (kf, "enabled");
  ok = bolt_store_config_save (tt->store, kf, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  g_clear_pointer (&loaded, g_key_file_unref);
  loaded = bolt_store_config_load (tt->store, &err);
  g_assert_no_error (err);
  g_assert_nonnull (loaded);

  tri = bolt_config_load_auth_mode (loaded, &authmode, &err);
  g_assert_no_error (err);
  g_assert (tri == TRI_YES);
  g_assert_cmpuint (authmode, ==, BOLT_AUTH_ENABLED);
}

static void
test_key (TestStore *tt, gconstpointer user_data)
{
  g_autoptr(BoltKey) key = NULL;
  g_autoptr(BoltKey) loaded = NULL;
  g_autoptr(GFile) base = NULL;
  g_autoptr(GFile) f = NULL;
  g_autoptr(GError) err = NULL;
  g_autoptr(GFileInfo) fi = NULL;
  g_autofree char *p = NULL;
  gboolean fresh = FALSE;
  gboolean ok;
  guint32 mode;
  int r;

  key = bolt_key_new (NULL);
  g_assert_nonnull (key);

  g_object_get (key, "fresh", &fresh, NULL);
  g_assert_true (fresh);
  fresh = bolt_key_get_state (key);
  g_assert_true (fresh);

  base = g_file_new_for_path (tt->path);
  f = g_file_get_child (base, "key");
  g_assert_nonnull (base);
  g_assert_nonnull (f);

  ok = bolt_key_save_file (key, f, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  fi = g_file_query_info (f, "*", 0, NULL, &err);
  g_assert_no_error (err);
  g_assert_nonnull (fi);

  mode = g_file_info_get_attribute_uint32 (fi, "unix::mode");
  g_assert_cmpuint (mode & 0666, ==, 0600);

  loaded = bolt_key_load_file (f, &err);
  g_assert_no_error (err);
  g_assert_nonnull (loaded);

  g_clear_object (&loaded);

  /* corrupt the key */
  p = g_file_get_path (f);
  r = truncate (p, 32);

  g_assert_cmpint (r, ==, 0);
  loaded = bolt_key_load_file (f, &err);
  g_assert_error (err, BOLT_ERROR, BOLT_ERROR_BADKEY);
  g_assert_null (loaded);
  g_clear_error (&err);

  /* empty key file ("", or "\n") */
  for (gssize i = 0; i < 2; i++)
    {
      ok = g_file_set_contents (p, "\n", i, &err);
      g_assert_no_error (err);
      g_assert_true (ok);

      loaded = bolt_key_load_file (f, &err);
      g_assert_error (err, BOLT_ERROR, BOLT_ERROR_NOKEY);
      g_assert_null (loaded);
      g_clear_error (&err);
    }
}

static void
test_store_invalid_data (TestStore *tt, gconstpointer user_data)
{
  g_autofree char *path = NULL;
  g_autofree char *fn = NULL;

  g_autoptr(GError) err = NULL;
  g_autoptr(BoltDevice) dev = NULL;
  static const char *uid = "399d33cb-c9cf-4273-8f92-9445437e0b43";
  TestContext *ctx = (TestContext *) user_data;
  gboolean ok;
  int r;

  path = g_build_filename (tt->path, "devices", NULL);
  r = g_mkdir (path, 0755);
  g_assert_true (r == 0);

  fn = g_build_filename (path, uid, NULL);
  ok = g_file_set_contents (fn, "", 0, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  test_context_set_logger (ctx, null_logger);
  dev = bolt_store_get_device (tt->store, uid, &err);
  test_context_set_logger (ctx, g_log_writer_default);

  g_assert_null (dev);
  g_assert_error (err, BOLT_ERROR, BOLT_ERROR_FAILED);
}

static void
test_store_times (TestStore *tt, gconstpointer user_data)
{
  g_autoptr(BoltDevice) dev = NULL;
  g_autoptr(BoltDevice) stored = NULL;
  g_autoptr(GError) error = NULL;
  char uid[] = "fbc83890-e9bf-45e5-a777-b3728490989c";
  guint64 authin = 574423871;
  guint64 connin = 574416000;
  guint64 authout;
  guint64 connout;
  gboolean ok;

  dev = g_object_new (BOLT_TYPE_DEVICE,
                      "uid", uid,
                      "name", "Laptop",
                      "vendor", "GNOME.org",
                      "status", BOLT_STATUS_DISCONNECTED,
                      "authtime", authin,
                      "conntime", connin,
                      NULL);

  stored = bolt_store_get_device (tt->store, uid, &error);
  g_assert_nonnull (error);
  g_assert_true (bolt_err_notfound (error));
  g_assert_null (stored);
  g_clear_error (&error);

  /* store the device with times */
  ok = bolt_store_put_device (tt->store, dev,
                              BOLT_POLICY_AUTO, NULL,
                              &error);
  g_assert_no_error (error);
  g_assert_true (ok);

  /* verify the store has recorded the times */
  ok = bolt_store_get_time (tt->store,
                            uid,
                            "authtime",
                            &authout,
                            &error);

  g_assert_no_error (error);
  g_assert_true (ok);
  g_assert_cmpuint (authout, ==, authin);

  ok = bolt_store_get_time (tt->store,
                            uid,
                            "conntime",
                            &connout,
                            &error);

  g_assert_no_error (error);
  g_assert_true (ok);
  g_assert_cmpuint (connout, ==, connin);

  /* check a newly loaded device has the times */
  stored = bolt_store_get_device (tt->store, uid, &error);
  g_assert_no_error (error);
  g_assert_nonnull (stored);

  connout = bolt_device_get_conntime (stored);
  authout = bolt_device_get_authtime (stored);

  g_assert_cmpuint (connout, ==, connin);
  g_assert_cmpuint (authout, ==, authin);

  /* update the times */
  connin = 8688720;
  authin = 9207120;

  ok = bolt_store_put_times (tt->store, uid, &error,
                             "conntime", connin,
                             "authtime", authin,
                             NULL);
  g_assert_no_error (error);
  g_assert_true (ok);



  /* verify via store */
  ok = bolt_store_get_time (tt->store, uid,
                            "authtime", &authout,
                            &error);

  g_assert_no_error (error);
  g_assert_true (ok);
  g_assert_cmpuint (authout, ==, authin);

  ok = bolt_store_get_time (tt->store, uid,
                            "conntime", &connout,
                            &error);

  g_assert_no_error (error);
  g_assert_true (ok);
  g_assert_cmpuint (connout, ==, connin);

  authout = connout = 0;

  bolt_store_get_times (tt->store, uid, &error,
                        "authtime", &authout,
                        "conntime", &connout,
                        NULL);

  g_assert_no_error (error);
  g_assert_true (ok);
  g_assert_cmpuint (connout, ==, connin);
  g_assert_cmpuint (authout, ==, authin);

  /* via the device loading */
  g_clear_object (&stored);
  stored = bolt_store_get_device (tt->store, uid, &error);
  g_assert_no_error (error);
  g_assert_nonnull (stored);

  connout = bolt_device_get_conntime (stored);
  authout = bolt_device_get_authtime (stored);

  g_assert_cmpuint (connout, ==, connin);
  g_assert_cmpuint (authout, ==, authin);


  /* check property access */
  connout = authout = 0;

  g_object_get (stored,
                "conntime", &connout,
                "authtime", &authout,
                NULL);

  g_assert_cmpuint (connout, ==, connin);
  g_assert_cmpuint (authout, ==, authin);

  /* lets remove them again */
  ok = bolt_store_del_time (tt->store, uid,
                            "conntime",
                            &error);

  g_assert_no_error (error);
  g_assert_true (ok);

  connout = 0;
  ok = bolt_store_get_time (tt->store, uid,
                            "conntime", &connout,
                            &error);

  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_assert_false (ok);
  g_clear_error (&error);

  /* the multiple timestamp version of del is
   * ignoring not found errors */
  ok = bolt_store_del_times (tt->store, uid, &error,
                             "authtime", "conntime",
                             NULL);

  g_assert_no_error (error);
  g_assert_true (ok);

  connout = 0;
  ok = bolt_store_get_time (tt->store, uid,
                            "authtime", &connout,
                            &error);

  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_assert_false (ok);
  g_clear_error (&error);

  /* check the time is not there, via the device loading */
  g_clear_object (&stored);
  stored = bolt_store_get_device (tt->store, uid, &error);
  g_assert_no_error (error);
  g_assert_nonnull (stored);

  connout = bolt_device_get_conntime (stored);

  g_assert_cmpuint (connout, ==, 0);
}

static void
test_store_domain (TestStore *tt, gconstpointer user_data)
{
  g_autoptr(GError) err = NULL;
  g_auto(GStrv) uids = NULL;
  g_autoptr(BoltDomain) d1 = NULL;
  g_autoptr(BoltDomain) s1 = NULL;
  const char *uid = "884c6edd-7118-4b21-b186-b02d396ecca0";
  gboolean exists;
  gboolean ok;
  GStrv bootacl = NULL;
  const char *acl[16] = {
    "884c6edd-7118-4b21-b186-b02d396ecca1",
    "884c6edd-7118-4b21-b186-b02d396ecca2",
    "",
    "884c6edd-7118-4b21-b186-b02d396ecca3",
    NULL,
  };

  uids = bolt_store_list_uids (tt->store, "domains", &err);

  g_assert_no_error (err);
  g_assert_nonnull (uids);
  g_assert_cmpuint (g_strv_length (uids), ==, 0);

  d1 = g_object_new (BOLT_TYPE_DOMAIN,
                     "uid", uid,
                     "bootacl", NULL,
                     NULL);

  g_assert_false (bolt_domain_is_stored (d1));
  g_assert_false (bolt_domain_supports_bootacl (d1));

  /* store */
  ok = bolt_store_put_domain (tt->store, d1, &err);
  g_assert_no_error (err);
  g_assert_true (ok);
  g_assert_true (bolt_domain_is_stored (d1));
  g_assert_false (bolt_domain_supports_bootacl (d1));

  g_clear_pointer (&uids, g_strfreev);

  /* list */
  uids = bolt_store_list_uids (tt->store, "domains", &err);
  g_assert_no_error (err);
  g_assert_nonnull (uids);
  g_assert_cmpuint (g_strv_length (uids), ==, 1);

  g_assert_cmpstr (uids[0], ==, uid);

  /* get */
  s1 = bolt_store_get_domain (tt->store, uid, &err);
  g_assert_no_error (err);
  g_assert_nonnull (s1);
  g_assert_true (bolt_domain_is_stored (s1));
  g_assert_false (bolt_domain_supports_bootacl (s1));

  g_assert_cmpstr (uid, ==, bolt_domain_get_uid (s1));
  bootacl = bolt_domain_get_bootacl (s1);
  g_assert_null (bootacl);

  /* update the bootacl */
  g_object_set (d1, "bootacl", acl, NULL);
  g_assert_true (bolt_domain_supports_bootacl (d1));

  ok = bolt_store_put_domain (tt->store, d1, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  exists = bolt_store_has_journal (tt->store, "bootacl", uid);
  g_assert_true (exists);

  ok = bolt_domain_can_delete (d1, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  /* update: get again after update */
  g_clear_object (&s1);
  s1 = bolt_store_get_domain (tt->store, uid, &err);
  g_assert_no_error (err);
  g_assert_nonnull (s1);
  g_assert_true (bolt_domain_is_stored (s1));
  g_assert_true (bolt_domain_supports_bootacl (d1));

  bootacl = bolt_domain_get_bootacl (s1);
  bolt_assert_strv_equal ((GStrv) acl, bootacl, 0);
  g_clear_object (&s1);

  /* delete */
  g_assert_true (bolt_domain_is_stored (d1));
  ok = bolt_store_del_domain (tt->store, d1, &err);
  g_assert_no_error (err);
  g_assert_true (ok);
  g_assert_false (bolt_domain_is_stored (d1));

  exists = bolt_store_has_journal (tt->store, "bootacl", uid);
  g_assert_false (exists);

  /* store again, modify the bootacl, i.e. write to the journal */
  ok = bolt_store_put_domain (tt->store, d1, &err);
  g_assert_no_error (err);
  g_assert_true (ok);
  g_assert_true (bolt_domain_is_stored (d1));

  ok = bolt_domain_bootacl_del (d1,
                                "884c6edd-7118-4b21-b186-b02d396ecca1",
                                &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  bootacl = bolt_domain_get_bootacl (d1);
  acl[0] = "";
  bolt_assert_strv_equal ((GStrv) acl, bootacl, 0);

  /* journal should exist and non-empty */
  exists = bolt_store_has_journal (tt->store, "bootacl", uid);
  g_assert_true (exists);

  /* non-empty journal should prevent deletion */
  ok = bolt_domain_can_delete (d1, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_NOT_EMPTY);
  g_assert_false (ok);
  g_clear_error (&err);

  ok = bolt_store_del_domain (tt->store, d1, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_NOT_EMPTY);
  g_assert_false (ok);
}

static void
test_store_journal (TestStore *tt, gconstpointer user_data)
{
  g_autoptr(GError) err = NULL;
  g_autoptr(BoltJournal) journal = NULL;
  gboolean exists;
  gboolean ok;

  /* delete an non-existing one */
  ok = bolt_store_del_journal (tt->store, "acl", "log", &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  /* create a non-existing one */
  journal = bolt_store_open_journal (tt->store, "acl", "log", &err);
  g_assert_no_error (err);
  g_assert_nonnull (journal);

  exists = bolt_store_has_journal (tt->store, "acl", "log");
  g_assert_true (exists);

  /* re-open the existing one */
  g_clear_object (&journal);
  journal = bolt_store_open_journal (tt->store, "acl", "log", &err);
  g_assert_no_error (err);
  g_assert_nonnull (journal);

  /* delete the journal */
  g_clear_object (&journal);
  ok = bolt_store_del_journal (tt->store, "acl", "log", &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  exists = bolt_store_has_journal (tt->store, "acl", "log");
  g_assert_false (exists);

}

static void
test_store_upgrade (TestStore *tt, gconstpointer user_data)
{
  g_autoptr(BoltDevice) dev = NULL;
  g_autoptr(GError) err = NULL;
  g_autoptr(DIR) root = NULL;
  char uid[] = "fbc83890-e9bf-45e5-a777-b3728490989c";
  guint version;
  gboolean up;
  gboolean ok;

  /* simulate a version 0 store, i.e. has some entries,
   * but no 'version' file */

  root = bolt_opendir (tt->path, &err);
  g_assert_no_error (err);
  g_assert_nonnull (root);

  version = bolt_store_get_version (tt->store);
  g_assert_cmpuint (version, ==, BOLT_STORE_VERSION);

  dev = g_object_new (BOLT_TYPE_DEVICE,
                      "uid", uid,
                      "name", "Laptop",
                      "vendor", "GNOME.org",
                      "status", BOLT_STATUS_DISCONNECTED,
                      NULL);

  ok = bolt_store_put_device (tt->store, dev, BOLT_POLICY_AUTO, NULL, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  /* close the store, delete 'version' */
  g_clear_object (&tt->store);

  ok = bolt_unlink_at (dirfd (root), "version", 0, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  /* re-create the store object */
  tt->store = bolt_store_new (tt->path, &err);
  g_assert_no_error (err);
  g_assert_nonnull (tt->store);

  version = bolt_store_get_version (tt->store);
  g_assert_cmpuint (version, ==, 0);

  /* no upgrade the store */
  ok = bolt_store_upgrade (tt->store, &up, &err);
  g_assert_no_error (err);
  g_assert_true (ok);
  g_assert_true (up);

  /* assert the upgrade changed the version */
  version = bolt_store_get_version (tt->store);
  g_assert_cmpuint (version, ==, BOLT_STORE_VERSION);

  /* upgrade again, check it did not do anything */
  ok = bolt_store_upgrade (tt->store, &up, &err);
  g_assert_no_error (err);
  g_assert_true (ok);
  g_assert_false (up);

  version = bolt_store_get_version (tt->store);
  g_assert_cmpuint (version, ==, BOLT_STORE_VERSION);

  /* ensure 'upgrade' argument is optional */
  ok = bolt_store_upgrade (tt->store, NULL, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

}

int
main (int argc, char **argv)
{
  TestContext test_context;

  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  g_log_set_writer_func (test_context_logger, &test_context, NULL);

  bolt_dbus_ensure_resources ();

  g_test_add ("/daemon/key",
              TestStore,
              &test_context,
              test_store_setup,
              test_key,
              test_store_tear_down);

  g_test_add ("/daemon/store/basic",
              TestStore,
              &test_context,
              test_store_setup,
              test_store_basic,
              test_store_tear_down);

  g_test_add ("/daemon/store/update",
              TestStore,
              &test_context,
              test_store_setup,
              test_store_update,
              test_store_tear_down);

  g_test_add ("/daemon/store/config",
              TestStore,
              &test_context,
              test_store_setup,
              test_store_config,
              test_store_tear_down);

  g_test_add ("/daemon/store/invalid_data",
              TestStore,
              &test_context,
              test_store_setup,
              test_store_invalid_data,
              test_store_tear_down);

  g_test_add ("/daemon/store/times",
              TestStore,
              &test_context,
              test_store_setup,
              test_store_times,
              test_store_tear_down);

  g_test_add ("/daemon/store/domain",
              TestStore,
              &test_context,
              test_store_setup,
              test_store_domain,
              test_store_tear_down);

  g_test_add ("/daemon/store/journal",
              TestStore,
              &test_context,
              test_store_setup,
              test_store_journal,
              test_store_tear_down);

  g_test_add ("/daemon/store/upgrade",
              TestStore,
              &test_context,
              test_store_setup,
              test_store_upgrade,
              test_store_tear_down);

  return g_test_run ();
}

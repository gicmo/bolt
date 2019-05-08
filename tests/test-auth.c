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

#include "bolt-auth.h"
#include "bolt-device.h"
#include "bolt-key.h"

#include "bolt-dbus.h"
#include "bolt-error.h"
#include "bolt-fs.h"
#include "bolt-io.h"
#include "bolt-log.h"
#include "bolt-str.h"
#include "bolt-time.h"

#include <gio/gio.h>

#include <string.h>
#include <locale.h>

typedef struct TestDummy
{
  int dummy;
} TestDummy;

static void
test_auth_basic (TestDummy *tt, gconstpointer user_data)
{
  g_autoptr(BoltAuth) auth = NULL;
  g_autoptr(BoltDevice) dev = NULL;
  g_autoptr(BoltDevice) d2 = NULL;
  g_autoptr(BoltKey) key = NULL;
  g_autoptr(BoltKey) k2 = NULL;
  g_autoptr(GObject) obj = NULL;
  BoltSecurity level;
  BoltPolicy policy;
  gpointer origin;
  char uid[] = "fbc83890-e9bf-45e5-a777-b3728490989c";

  dev = g_object_new (BOLT_TYPE_DEVICE,
                      "uid", uid,
                      "name", "Laptop",
                      "vendor", "GNOME.org",
                      "status", BOLT_STATUS_DISCONNECTED,
                      NULL);
  g_assert_nonnull (dev);

  key = bolt_key_new (NULL);
  g_assert_nonnull (key);

  auth = bolt_auth_new (dev, BOLT_SECURITY_SECURE, key);
  g_object_set (auth, "device", dev, NULL);

  policy = BOLT_POLICY_DEFAULT; // we expect _UNKNOWN
  g_object_get (auth,
                "origin", &origin,
                "level", &level,
                "key", &k2,
                "device", &d2,
                "policy", &policy,
                NULL);

  g_assert_cmpint (level, ==, BOLT_SECURITY_SECURE);
  g_assert_true (origin == dev);
  g_assert_true (dev == bolt_auth_get_device (auth));
  g_assert_true (key == k2);
  g_assert_true (dev == d2);
  g_assert_cmpint (policy, ==, BOLT_POLICY_UNKNOWN);

  g_assert_true (G_IS_ASYNC_RESULT (auth));
  obj = g_async_result_get_source_object (G_ASYNC_RESULT (auth));
  g_assert ((gpointer) obj == (gpointer) dev);
  g_assert_false (g_async_result_is_tagged (G_ASYNC_RESULT (auth), &obj));
  g_assert_null (g_async_result_get_user_data (G_ASYNC_RESULT (auth)));

  bolt_auth_set_policy (auth, BOLT_POLICY_MANUAL);
  policy = bolt_auth_get_policy (auth);
  g_assert_cmpint (policy, ==, BOLT_POLICY_MANUAL);
  g_object_set (auth, "policy", BOLT_POLICY_AUTO, NULL);
  policy = BOLT_POLICY_UNKNOWN;
  g_object_get (auth, "policy", &policy, NULL);
  g_assert_cmpint (policy, ==, BOLT_POLICY_AUTO);

}

static void
test_auth_error (TestDummy *tt, gconstpointer user_data)
{
  g_autoptr(GError) err = NULL;
  g_autoptr(BoltAuth) auth = NULL;
  g_autoptr(BoltDevice) dev = NULL;
  g_autoptr(BoltKey) key = NULL;
  //  BoltSecurity level;
  //gpointer origin;
  char uid[] = "fbc83890-e9bf-45e5-a777-b3728490989c";
  gboolean ok;

  dev = g_object_new (BOLT_TYPE_DEVICE,
                      "uid", uid,
                      "name", "Laptop",
                      "vendor", "GNOME.org",
                      "status", BOLT_STATUS_DISCONNECTED,
                      NULL);
  g_assert_nonnull (dev);

  key = bolt_key_new (NULL);
  g_assert_nonnull (key);

  auth = bolt_auth_new (dev, BOLT_SECURITY_SECURE, key);
  bolt_auth_return_new_error (auth, BOLT_ERROR, BOLT_ERROR_BADSTATE,
                              "we are in a bad state: %s", "depressed");

  ok = bolt_auth_check (auth, &err);
  g_assert_error (err, BOLT_ERROR, BOLT_ERROR_BADSTATE);
  g_assert_false (ok);
  g_clear_pointer (&err, g_error_free);

  /* error should be copied, so we should be able to call it twice */
  ok = bolt_auth_check (auth, &err);
  g_assert_error (err, BOLT_ERROR, BOLT_ERROR_BADSTATE);
  g_assert_false (ok);
  g_clear_pointer (&err, g_error_free);

  /*  */
  g_clear_object (&auth);
  auth = bolt_auth_new (dev, BOLT_SECURITY_SECURE, key);
  g_set_error (&err, BOLT_ERROR, BOLT_ERROR_AUTHCHAIN,
               "we are in a bad state: %s", "depressed");
  bolt_auth_return_error (auth, &err);
  g_assert_no_error (err);

  ok = bolt_auth_check (auth, &err);
  g_assert_error (err, BOLT_ERROR, BOLT_ERROR_AUTHCHAIN);
  g_assert_false (ok);
  g_clear_pointer (&err, g_error_free);

}


int
main (int argc, char **argv)
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  bolt_dbus_ensure_resources ();

  g_test_add ("/auth/basic",
              TestDummy,
              NULL,
              NULL,
              test_auth_basic,
              NULL);

  g_test_add ("/auth/error",
              TestDummy,
              NULL,
              NULL,
              test_auth_error,
              NULL);


  return g_test_run ();
}

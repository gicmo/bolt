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

#include "bolt-device.h"

#include "bolt-dbus.h"
#include "bolt-domain.h"
#include "bolt-store.h"

#include "bolt-error.h"

#include <locale.h>

typedef struct
{
  int dummy;
} TestDevice;

static void
test_device_basic (TestDevice *tt, gconstpointer user_data)
{
  g_autoptr(BoltDevice) dev = NULL;
  g_autoptr(BoltDomain) dom = NULL;
  g_autoptr(BoltStore) store = NULL;
  g_autoptr(BoltKey) key = NULL;
  g_autoptr(GError) err = NULL;
  char uid[] = "fbc83890-e9bf-45e5-a777-b3728490989c";
  BoltDeviceType devtype = BOLT_DEVICE_PERIPHERAL;
  BoltKeyState keystate;
  BoltSecurity sl;
  guint gen;
  gboolean ok;

  dev = g_object_new (BOLT_TYPE_DEVICE,
                      "uid", uid,
                      "name", "Laptop",
                      "vendor", "GNOME.org",
                      "type", BOLT_DEVICE_HOST,
                      "status", BOLT_STATUS_DISCONNECTED,
                      "generation", 3,
                      NULL);

  g_assert_nonnull (dev);

  g_object_get (dev,
                "store", &store,
                "domain", &dom,
                "security", &sl,
                "generation", &gen,
                "type", &devtype,
                NULL);

  g_assert_null (store);
  g_assert_null (dom);

  g_assert_true (dom == bolt_device_get_domain (dev));
  g_assert_cmpint (sl, ==, BOLT_SECURITY_UNKNOWN);
  g_assert_cmpint (gen, ==, 3);
  g_assert_cmpint (gen, ==, bolt_device_get_generation (dev));
  g_assert_cmpint (devtype, ==, BOLT_DEVICE_HOST);
  g_assert_true (bolt_device_is_host (dev));

  keystate = bolt_device_get_keystate (dev);
  g_assert_cmpint (keystate, ==, BOLT_KEY_MISSING);

  ok = bolt_device_get_key_from_sysfs (dev, &key, &err);
  g_assert_error (err, BOLT_ERROR, BOLT_ERROR_BADSTATE);
  g_assert_false (ok);
}

int
main (int argc, char **argv)
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  bolt_dbus_ensure_resources ();

  g_test_add ("/device/basic",
              TestDevice,
              NULL,
              NULL,
              test_device_basic,
              NULL);

  return g_test_run ();
}

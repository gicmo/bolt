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

#include "store.h"

#include <locale.h>

typedef struct Fixture
{
  TbStore *store;
} Fixture;

typedef struct Params
{
  const char *path;
} Params;

static void
fixture_set_up (Fixture *fixture, gconstpointer user_data)
{
  const Params *p = user_data;

  fixture->store = tb_store_new (p->path);
}

static void
fixture_tear_down (Fixture *fixture, gconstpointer user_data)
{
  g_object_unref (fixture->store);
}

static void
test_store_basic (Fixture *fixture, gconstpointer user_data)
{
  //  const Params *p = user_data;
  g_autoptr(GError) err   = NULL;
  g_autoptr(TbDevice) dev = NULL;
  g_autoptr(GFile) key    = NULL;
  g_autofree char *data   = NULL;
  g_autofree char *uuid   = NULL;
  gsize len               = 0;
  gboolean ok;

  uuid = g_uuid_string_random ();

  dev = g_object_new (TB_TYPE_DEVICE, "uid", uuid, "device-name", "Blitz", "vendor-name", "GNOME", NULL);

  g_debug ("Storing device: %s", uuid);
  ok = tb_store_put (fixture->store, dev, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  g_debug ("Generating key");
  ok = tb_store_create_key (fixture->store, dev, &err);
  g_assert_no_error (err);
  g_assert_true (ok);
  g_assert_true (tb_device_have_key (dev));
  key = tb_device_get_key (dev);
  g_assert_nonnull (key);

  ok = g_file_load_contents (key, NULL, &data, &len, NULL, &err);

  g_assert_no_error (err);
  g_assert_true (ok);

  g_debug ("Key: [%lu] %s", len, data);
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

  g_test_add ("/store/basic", Fixture, &p, fixture_set_up, test_store_basic, fixture_tear_down);

  return g_test_run ();
}

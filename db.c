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

#include <gio/gio.h>
#include <gudev/gudev.h>

#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "device.h"
#include "store.h"

static void
print_device (TbDevice *dev)
{
  const char *uid    = tb_device_get_uid (dev);
  const char *name   = tb_device_get_name (dev);
  const char *vendor = tb_device_get_vendor_name (dev);
  gboolean autoauth  = tb_device_autoconnect (dev);

  g_print ("%s\n", name);
  g_print ("  ├─ vendor: %s\n", vendor);
  g_print ("  ├─ uuid:   %s\n", uid);
  g_print ("  └─ auto:   %s\n", autoauth ? "yes" : "no");
  g_print ("\n");
}

static int
list (TbStore *store)
{
  g_autoptr(GError) error = NULL;
  g_auto(GStrv) ids       = NULL;
  char **id;

  ids = tb_store_list_ids (store, &error);
  if (ids == NULL)
    return EXIT_FAILURE;

  for (id = ids; *id; id++)
    {
      g_autoptr(TbDevice) dev = tb_store_get (store, *id, &error);

      if (dev == NULL)
        continue;

      print_device (dev);
    }

  return EXIT_SUCCESS;
}

static char *known_fields[] = {"auto"};

static int
get (TbStore *store, int argc, char **argv)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(TbDevice) dev = NULL;
  const char *uid;
  char **fields;
  int n, i;

  if (argc < 3)
    return EXIT_FAILURE;

  uid = argv[2];

  dev = tb_store_get (store, uid, &error);

  if (!dev)
    {
      g_printerr ("Could not get device: %s", error->message);
      return EXIT_FAILURE;
    }

  if (argc > 3)
    {
      fields = argv + 3;
      n      = argc - 3;
    }
  else
    {
      fields = known_fields;
      n      = G_N_ELEMENTS (known_fields);
    }

  for (i = 0; i < n; i++)
    {
      const char *field = fields[i];
      if (g_str_equal (field, "auto"))
        {
          gboolean do_auto = tb_device_autoconnect (dev);
          g_print ("auto: %s\n", do_auto ? "yes" : "no");
        }
      else
        {
          g_warning ("Unknown field: %s", field);
        }
    }

  return EXIT_SUCCESS;
}

static gboolean
string_to_bool (const char *str, gboolean *b)
{
  if (!g_ascii_strcasecmp (str, "yes"))
    {
      *b = TRUE;
      return TRUE;
    }
  else if (!g_ascii_strcasecmp (str, "no"))
    {
      *b = FALSE;
      return TRUE;
    }

  return FALSE;
}

static int
set (TbStore *store, int argc, char **argv)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(TbDevice) dev = NULL;
  gboolean ok;
  const char *uid;
  char **fields;
  int n, i;

  if (argc < 3)
    return EXIT_FAILURE;

  uid = argv[2];

  dev = tb_store_get (store, uid, &error);

  if (!dev)
    {
      g_printerr ("Could not get device: %s", error->message);
      return EXIT_FAILURE;
    }

  if (argc > 3)
    {
      fields = argv + 3;
      n      = argc - 3;
    }
  else
    {
      fields = known_fields;
      n      = G_N_ELEMENTS (known_fields);
    }

  for (i = 0; i < n; i++)
    {
      const char *field   = fields[i];
      g_auto(GStrv) split = g_strsplit (field, "=", 2);
      const char *key, *value;
      if (g_strv_length (split) != 2)
        {
          g_printerr ("wrong argument: %s\n", field);
          return EXIT_FAILURE;
        }

      key   = split[0];
      value = split[1];

      if (g_str_equal (key, "auto"))
        {
          gboolean b;

          ok = string_to_bool (value, &b);
          if (!ok)
            {
              g_fprintf (stderr, "Could not convert '%s' to boolean\n", value);
              return EXIT_FAILURE;
            }

          dev->autoconnect = b;
        }
    }

  ok = tb_store_put (store, dev, &error);

  if (!ok)
    g_printerr ("Could not store device changes: %s\n", error->message);

  return EXIT_SUCCESS;
}

int
main (int argc, char **argv)
{
  g_autoptr(GError) error          = NULL;
  g_autoptr(TbStore) store         = NULL;
  g_autoptr(GOptionContext) optctx = NULL;
  g_autoptr(GFile) root            = NULL;
  const char *cmd;
  int res = EXIT_FAILURE;

  setlocale (LC_ALL, "");

  optctx = g_option_context_new (NULL);

  g_option_context_set_summary (optctx, "Authorize thunderbolt devices");
  g_option_context_set_strict_posix (optctx, TRUE);

  if (!g_option_context_parse (optctx, &argc, &argv, &error))
    {
      g_printerr ("Failed to parse command line arguments.");
      return EXIT_FAILURE;
    }

  if (argc < 2)
    {
      g_printerr ("Usage %s COMMAND\n", argv[0]);
      return EXIT_FAILURE;
    }

  if (getuid () != 0 || geteuid () != 0)
    {
      g_printerr ("Need root permissions to authenticate.\n");
      return EXIT_FAILURE;
    }

  cmd = argv[1];

  root  = g_file_new_for_path ("/var/lib/tb");
  store = g_object_new (TB_TYPE_STORE, "root", root, NULL);

  if (g_str_equal (cmd, "list"))
    res = list (store);
  else if (g_str_equal (cmd, "get"))
    res = get (store, argc, argv);
  else if (g_str_equal (cmd, "set"))
    res = set (store, argc, argv);
  else
    g_printerr ("Unknown command.\n");

  return res;
}

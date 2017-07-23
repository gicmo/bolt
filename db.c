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
    {
      g_auto(GStrv) ids = tb_store_list_ids (store, &error);
      char **id;

      if (ids == NULL)
        return EXIT_FAILURE;

      for (id = ids; *id; id++)
        {
          g_autoptr(TbDevice) dev = tb_store_get (store, *id, &error);

          if (dev == NULL)
            continue;

          print_device (dev);
        }

      res = EXIT_SUCCESS;
    }
  else
    {
      g_printerr ("Unknown command.\n");
    }

  return res;
}

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

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>

#include "device.h"
#include "manager.h"

static int
monitor (TbManager *mgr)
{
  GMainLoop *loop;

  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);
  return 0;
}

static void
device_print (TbDevice *dev)
{
  const char *uid        = tb_device_get_uid (dev);
  const char *name       = tb_device_get_name (dev);
  const char *vendor     = tb_device_get_vendor_name (dev);
  gint authorized        = tb_device_get_authorized (dev);
  gboolean is_authorized = authorized > 0;
  gboolean in_store      = tb_device_in_store (dev);
  gboolean autoauth      = tb_device_autoconnect (dev);

  g_print ("\033[1;%dm●\033[0m %s\n", is_authorized ? 32 : 31, name);
  g_print ("  ├─ vendor:     %s\n", vendor);
  g_print ("  ├─ uuid:       %s\n", uid);
  g_print ("  ├─ authorized: %d\n", authorized);
  g_print ("  └─ in store:   %s\n", in_store ? "yes" : "no");
  if (in_store)
    g_print ("      └─ auto:   %s\n", autoauth ? "yes" : "no");
  g_print ("\n");
}

static int
list_devices_attached (TbManager *mgr)
{
  const GPtrArray *devices;
  guint i;

  devices = tb_manager_list_attached (mgr);

  for (i = 0; i < devices->len; i++)
    {
      TbDevice *dev = g_ptr_array_index (devices, i);
      device_print (dev);
    }

  return 0;
}

typedef int (*subcommand_t)(TbManager *mgr);

typedef struct Cmd
{
  const char  *name;
  subcommand_t fn;
} Cmd;

static Cmd subcommands[] = {{"list", list_devices_attached}, {"monitor", monitor}, {NULL, NULL}};

int
main (int argc, char **argv)
{
  g_autoptr(GError) error          = NULL;
  g_autoptr(TbManager) mgr         = NULL;
  g_autoptr(GOptionContext) optctx = NULL;
  Cmd *cmd;

  setlocale (LC_ALL, "");

  optctx = g_option_context_new (NULL);

  g_option_context_set_summary (optctx, "Manage thunderbolt devices");
  g_option_context_set_strict_posix (optctx, TRUE);

  if (!g_option_context_parse (optctx, &argc, &argv, &error))
    {
      g_printerr ("Failed to parse command line arguments");
      return EXIT_FAILURE;
    }

  if (argc < 2)
    return EXIT_FAILURE;

  mgr = tb_manager_new (&error);

  if (!mgr)
    {
      g_error ("Could not create manager: %s", error->message);
      return -1;
    }

  for (cmd = subcommands; cmd->name; cmd++)
    if (g_str_equal (argv[1], cmd->name))
      break;

  if (!cmd->fn)
    return EXIT_FAILURE;

  return cmd->fn (mgr);
}
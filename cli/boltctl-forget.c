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

#include "boltctl-cmds.h"

#include "bolt-str.h"

#include <stdlib.h>

int
forget (BoltClient *client, int argc, char **argv)
{
  g_autoptr(GOptionContext) optctx = NULL;
  g_autoptr(GError) error = NULL;
  const char *uid;
  gboolean ok, forget_all = FALSE;
  GOptionEntry options[] = {
    { "all", 'a', 0, G_OPTION_ARG_NONE, &forget_all, "Forget all devices", NULL },
    { NULL }
  };

  optctx = g_option_context_new ("DEVICE - Remove a device from the store");
  g_option_context_add_main_entries (optctx, options, NULL);

  if (!g_option_context_parse (optctx, &argc, &argv, &error))
    return usage_error (error);

  if (forget_all)
    {
      g_autoptr(GPtrArray) devices = NULL;

      ok = check_argc (argc, 0, 0, &error);
      if (!ok)
        return usage_error (error);

      devices = bolt_client_list_devices (client, NULL, &error);
      if (devices == NULL)
        {
          g_printerr ("Failed to list devices: %s",
                      error->message);
          return EXIT_FAILURE;
        }

      for (guint i = 0; i < devices->len; i++)
        {
          BoltDevice *dev = g_ptr_array_index (devices, i);

          if (!bolt_device_is_stored (dev))
            continue;

          uid = bolt_device_get_uid (dev);
          ok = bolt_client_forget_device (client, uid, &error);
          if (!ok)
            {
              g_printerr ("Failed to forget device: %s\n", error->message);
              return EXIT_FAILURE;
            }
        }
    }
  else
    {
      if (argc < 2)
        return usage_error_need_arg ("DEVICE");
      uid = argv[1];
      ok = bolt_client_forget_device (client, uid, &error);
      if (!ok)
        {
          g_printerr ("Failed to forget device: %s\n", error->message);
          return EXIT_FAILURE;
        }
    }

  return EXIT_SUCCESS;
}

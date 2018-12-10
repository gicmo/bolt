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

#include "bolt-error.h"
#include "bolt-str.h"

#include <stdlib.h>

int
authorize (BoltClient *client, int argc, char **argv)
{
  g_autoptr(GOptionContext) optctx = NULL;
  g_autoptr(BoltDevice) dev = NULL;
  g_autoptr(GError) error = NULL;
  BoltAuthCtrl flags = BOLT_AUTHCTRL_NONE;
  BoltStatus status;
  const char *uid;
  gboolean ok;
  gboolean first_time = FALSE;
  GOptionEntry options[] = {
    { "first-time", 'F', 0, G_OPTION_ARG_NONE, &first_time, "Fail if device is already authorized", NULL },
    { NULL }
  };

  optctx = g_option_context_new ("DEVICE - Authorize a device");
  g_option_context_add_main_entries (optctx, options, NULL);

  if (!g_option_context_parse (optctx, &argc, &argv, &error))
    return usage_error (error);

  if (argc < 2)
    return usage_error_need_arg ("DEVICE");

  uid = argv[1];

  dev = bolt_client_get_device (client, uid, NULL, &error);
  if (dev == NULL)
    {
      g_printerr ("%s\n", error->message);
      return EXIT_FAILURE;
    }

  status = bolt_device_get_status (dev);

  ok = bolt_device_authorize (dev, flags, NULL, &error);
  if (!ok)
    {
      if (bolt_err_badstate (error) &&
          bolt_status_is_authorized (status) &&
          !first_time)
        ok = TRUE;
      else
        g_printerr ("Authorization error: %s\n", error->message);
    }

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

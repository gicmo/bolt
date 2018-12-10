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
enroll (BoltClient *client, int argc, char **argv)
{
  g_autoptr(GOptionContext) optctx = NULL;
  g_autoptr(BoltDevice) dev = NULL;
  g_autoptr(GError) error = NULL;
  const char *uid;
  BoltPolicy policy = BOLT_POLICY_DEFAULT;
  BoltAuthCtrl flags = BOLT_AUTHCTRL_NONE;
  const char *policy_arg = "default";
  GOptionEntry options[] = {
    { "policy", 0, 0, G_OPTION_ARG_STRING, &policy_arg, "Policy for the device; one of {auto, manual, *default}", "POLICY" },
    { NULL }
  };

  optctx = g_option_context_new ("DEVICE - Authorize and store a device in the database");
  g_option_context_add_main_entries (optctx, options, NULL);

  if (!g_option_context_parse (optctx, &argc, &argv, &error))
    return usage_error (error);

  if (argc < 2)
    return usage_error_need_arg ("DEVICE");

  policy = bolt_policy_from_string (policy_arg);

  if (!bolt_policy_validate (policy))
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "invalid policy '%s'", policy_arg);
      return usage_error (error);
    }

  uid = argv[1];

  dev = bolt_client_enroll_device (client, uid, policy, flags, &error);
  if (dev == NULL)
    {
      g_printerr ("%s\n", error->message);
      return EXIT_FAILURE;
    }

  print_device (dev, TRUE);
  return EXIT_SUCCESS;
}

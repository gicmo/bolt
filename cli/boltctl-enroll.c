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

typedef struct EnrollAllData
{
  GMainLoop *loop;
  gboolean   done;
  gboolean   res;
} EnrollAllData;

static void
enroll_all_done (GObject      *source,
                 GAsyncResult *res,
                 gpointer      user_data)
{
  g_autoptr(GError) err = NULL;
  EnrollAllData *data = (EnrollAllData *) user_data;
  gboolean ok;

  ok = bolt_client_enroll_all_finish (BOLT_CLIENT (source), res, &err);

  if (!ok)
    g_printerr ("Could not enroll all devices: %s\n", err->message);

  data->done = TRUE;
  data->res = ok;
  g_main_loop_quit (data->loop);
}

static int
enroll_all (BoltClient  *client,
            const char  *uid,
            BoltPolicy   policy,
            BoltAuthCtrl flags)
{
  g_autoptr(BoltDevice) target = NULL;
  g_autoptr(GPtrArray) parents = NULL;
  g_autoptr(GPtrArray) uuids = NULL;
  g_autoptr(GMainLoop) loop = NULL;
  g_autoptr(GError) err = NULL;
  EnrollAllData data;

  target = bolt_client_get_device (client,
                                   uid,
                                   NULL,
                                   &err);

  if (target == NULL)
    {
      g_printerr ("Could not look up target: %s\n",
                  err->message);
      return EXIT_FAILURE;
    }

  parents = bolt_client_list_parents (client, target, NULL, &err);
  if (parents == NULL)
    {
      g_printerr ("Could not look up parents: %s\n",
                  err->message);
      return EXIT_FAILURE;
    }

  uuids = g_ptr_array_new_full (parents->len, NULL);

  /* reverse order, starting from the root! */
  for (guint i = parents->len; i > 0; i--)
    {
      BoltDevice *dev = g_ptr_array_index (parents, i - 1);
      const char *id = bolt_device_get_uid (dev);

      if (bolt_device_is_stored (dev) ||
          bolt_device_is_host (dev))
        continue;

      g_ptr_array_add (uuids, (gpointer) id);
    }

  /* the target itself */
  g_ptr_array_add (uuids, (gpointer) uid);

  loop = g_main_loop_new (NULL, FALSE);
  data.loop = loop;
  data.done = FALSE;

  bolt_client_enroll_all_async (client,
                                uuids,
                                policy,
                                flags,
                                NULL,
                                enroll_all_done,
                                &data);

  if (data.done == FALSE)
    g_main_loop_run (loop);

  return data.res ? EXIT_SUCCESS : EXIT_FAILURE;
}

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
  gboolean chain_arg = FALSE;
  GOptionEntry options[] = {
    { "policy", 0, 0, G_OPTION_ARG_STRING, &policy_arg, "Policy for the device; one of {auto, manual, *default}", "POLICY" },
    { "chain", 0, 0, G_OPTION_ARG_NONE, &chain_arg, "Authorize parent devices if necessary" },
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

  if (chain_arg)
    return enroll_all (client, uid, policy, flags);


  dev = bolt_client_enroll_device (client, uid, policy, flags, &error);
  if (dev == NULL)
    {
      g_printerr ("%s\n", error->message);
      return EXIT_FAILURE;
    }

  print_device (dev, TRUE);
  return EXIT_SUCCESS;
}

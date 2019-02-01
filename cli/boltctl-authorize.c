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

typedef struct AuthorizeAllData
{
  GMainLoop *loop;
  gboolean   done;
  gboolean   res;

  GError   **error;
} AuthorizeAllData;

static void
authorize_all_done (GObject      *source,
                    GAsyncResult *res,
                    gpointer      user_data)
{
  AuthorizeAllData *data = (AuthorizeAllData *) user_data;
  gboolean ok;

  ok = bolt_client_authorize_all_finish (BOLT_CLIENT (source),
                                         res,
                                         data->error);

  data->res = ok;
  data->done = TRUE;

  g_main_loop_quit (data->loop);
}

static gboolean
authorize_all (BoltClient  *client,
               const char  *uid,
               BoltAuthCtrl flags,
               GError     **error)
{
  g_autoptr(BoltDevice) target = NULL;
  g_autoptr(BoltDevice) dev = NULL;
  g_autoptr(GPtrArray) uuids = NULL;
  g_autoptr(GMainLoop) loop = NULL;
  g_autoptr(GError) err = NULL;
  AuthorizeAllData data;
  const char *parent;

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

  uuids = g_ptr_array_new_full (16, (GDestroyNotify) g_free);

  dev = g_object_ref (target);

  while ((parent = bolt_device_get_parent (dev)) != NULL)
    {
      g_autofree char *up;
      BoltStatus status;

      up = g_strdup (parent);
      g_clear_object (&dev); /* parent is invalid */
      parent = NULL;
      dev = bolt_client_get_device (client,
                                    up,
                                    NULL,
                                    &err);

      if (dev == NULL)
        {
          g_printerr ("Could not look up parents: %s\n",
                      err->message);
          return EXIT_FAILURE;
        }

      status = bolt_device_get_status (dev);

      /* if the device is authorized, we are done */
      if (!bolt_status_is_pending (status))
        break;

      g_ptr_array_add (uuids, g_steal_pointer (&up));
    }

  /* the target itself */
  g_ptr_array_add (uuids, g_strdup (uid));

  loop = g_main_loop_new (NULL, FALSE);
  data.loop = loop;
  data.done = FALSE;
  data.error = error;

  bolt_client_authorize_all_async (client,
                                   uuids,
                                   flags,
                                   NULL,
                                   authorize_all_done,
                                   &data);

  if (data.done == FALSE)
    g_main_loop_run (loop);

  return data.res;
}


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
  gboolean chain_arg = FALSE;
  GOptionEntry options[] = {
    { "first-time", 'F', 0, G_OPTION_ARG_NONE, &first_time, "Fail if device is already authorized", NULL },
    { "chain", 0, 0, G_OPTION_ARG_NONE, &chain_arg, "Authorize parent devices if necessary" },
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

  if (chain_arg)
    ok = authorize_all (client, uid, flags, &error);
  else
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

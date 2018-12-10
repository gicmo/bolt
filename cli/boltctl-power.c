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

static gboolean
quit_main_loop (gpointer user_data)
{
  GMainLoop *loop = user_data;

  g_main_loop_quit (loop);
  return FALSE;
}

int
power (BoltClient *client, int argc, char **argv)
{
  g_autoptr(GOptionContext) optctx = NULL;
  g_autoptr(GMainLoop) main_loop = NULL;
  g_autoptr(BoltPower) power = NULL;
  g_autoptr(GError) error = NULL;
  BoltPowerState state;
  gboolean do_query = FALSE;
  int fd;
  double timeout = 0;
  GOptionEntry options[] = {
    { "query", 'q', 0, G_OPTION_ARG_NONE, &do_query, "Query the status", NULL },
    { "timeout", 't', 0, G_OPTION_ARG_DOUBLE, &timeout, "Quit after N seconds", NULL },
    { NULL }
  };

  optctx = g_option_context_new ("- Force power configuration");
  g_option_context_add_main_entries (optctx, options, NULL);

  if (!g_option_context_parse (optctx, &argc, &argv, &error))
    return usage_error (error);

  power = bolt_client_new_power_client (client, NULL, &error);

  if (power == NULL)
    {
      g_warning ("Could get proxy for power interface: %s",
                 error->message);
      return EXIT_FAILURE;
    }

  if (do_query)
    {
      g_autoptr(GPtrArray) guards = NULL;
      const char *tree_branch;
      const char *tree_right;
      const char *str;
      gboolean supported;

      supported = bolt_power_is_supported (power);
      g_print ("supported: %s\n", bolt_yesno (supported));

      if (!supported)
        return EXIT_SUCCESS;

      state = bolt_power_get_state (power);
      str = bolt_power_state_to_string (state);
      g_print ("power state: %s\n", str);

      guards = bolt_power_list_guards (power, NULL, &error);

      if (guards == NULL)
        {
          g_warning ("Could not list guards: %s", error->message);
          return EXIT_FAILURE;
        }

      tree_branch = bolt_glyph (TREE_BRANCH);
      tree_right = bolt_glyph (TREE_RIGHT);

      g_print ("%u active power guards%s\n", guards->len,
               guards->len > 0 ? ":" : "");

      for (guint i = 0; i < guards->len; i++)
        {
          BoltPowerGuard *g = g_ptr_array_index (guards, 0);
          g_print ("  guard '%s'\n", g->id);
          g_print ("   %s who: %s\n", tree_branch, g->who);
          g_print ("   %s pid: %u\n", tree_right, g->pid);
          g_print ("\n");
        }

      return EXIT_SUCCESS;
    }

  fd = bolt_power_force_power (power, &error);
  if (fd == -1)
    {
      g_warning ("Could force power controller: %s", error->message);
      return EXIT_SUCCESS;
    }

  g_print ("acquired power guard (%d)\n", fd);

  main_loop = g_main_loop_new (NULL, FALSE);

  if (timeout > 0.)
    {
      guint tout = (guint) timeout * 1000;
      g_timeout_add (tout, quit_main_loop, main_loop);
    }

  g_main_loop_run (main_loop);

  (void) close (fd);
  return EXIT_SUCCESS;
}

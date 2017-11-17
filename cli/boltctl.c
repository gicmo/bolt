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

#include "bolt-client.h"
#include "bolt-enums.h"
#include "bolt-error.h"
#include "bolt-term.h"

#include <gio/gio.h>

#include <locale.h>
#include <stdlib.h>

static const char *
yes_no (gboolean b)
{
  return b ? "yes" : "no ";
}

static void
print_device (BoltDevice *dev, gboolean verbose)
{
  g_autofree char *path = NULL;
  g_autofree char *uid = NULL;
  g_autofree char *name = NULL;
  g_autofree char *vendor = NULL;
  g_autofree char *syspath = NULL;
  BoltSecurity security = BOLT_SECURITY_NONE;
  BoltStatus status;
  BoltKeyState keystate;
  BoltPolicy policy;
  const char *status_color;
  const char *status_symbol;
  const char *status_text;
  const char *tree_branch;
  const char *tree_right;
  const char *tree_space;
  gboolean stored;

  g_object_get (dev,
                "object-path", &path,
                "name", &name,
                "vendor", &vendor,
                "status", &status,
                "uid", &uid,
                "security", &security,
                "syspath", &syspath,
                "stored", &stored,
                "policy", &policy,
                "key", &keystate,
                NULL);

  status_symbol = bolt_glyph (BLACK_CIRCLE);
  tree_branch = bolt_glyph (TREE_BRANCH);
  tree_right = bolt_glyph (TREE_RIGHT);
  tree_space = bolt_glyph (TREE_SPACE);

  switch (status)
    {
    case BOLT_STATUS_DISCONNECTED:
      status_symbol = bolt_glyph (WHITE_CIRCLE);
      status_color = bolt_color (ANSI_NORMAL);
      status_text = "disconnected";
      break;

    case BOLT_STATUS_CONNECTED:
      status_color = bolt_color (ANSI_YELLOW);
      status_text = "connected";
      break;

    case BOLT_STATUS_AUTHORIZED:
    case BOLT_STATUS_AUTHORIZED_NEWKEY:
    case BOLT_STATUS_AUTHORIZED_SECURE:
      status_color = bolt_color (ANSI_GREEN);
      status_text = "authorized";
      break;

    case BOLT_STATUS_AUTH_ERROR:
      status_color = bolt_color (ANSI_RED);
      status_text = "authorization error";
      break;

    default:
      status_color = bolt_color (ANSI_NORMAL);
      status_text = "unknown";
      break;
    }

  g_print (" %s%s%s %s\n",
           status_color,
           status_symbol,
           bolt_color (ANSI_NORMAL),
           name);

  g_print ("   %s uuid:        %s\n", tree_branch, uid);
  g_print ("   %s vendor:      %s\n", tree_branch, vendor);
  if (verbose)
    g_print ("   %s dbus path:   %s\n", tree_branch, path);
  g_print ("   %s status:      %s\n", tree_branch, status_text);

  if (bolt_status_is_connected (status))
    {
      if (verbose)
        {
          g_print ("   %s %s syspath:  %s\n",
                   bolt_glyph (TREE_VERTICAL),
                   tree_branch,
                   syspath);
        }
      g_print ("   %s %s security: %s\n",
               bolt_glyph (TREE_VERTICAL),
               tree_right,
               bolt_security_to_string (security));
    }

  g_print ("   %s stored:      %s\n", tree_right, yes_no (stored));

  if (stored)
    {
      const char *pstr = bolt_policy_to_string (policy);
      const char *kstr;

      if (keystate == BOLT_KEY_MISSING)
        kstr = "no";
      else if (keystate == BOLT_KEY_HAVE)
        kstr = "yes";
      else if (keystate == BOLT_KEY_NEW)
        kstr = "yes (new)";
      else
        kstr = "unknown";

      g_print ("   %s %s policy:   %s\n", tree_space, tree_branch, pstr);
      g_print ("   %s %s key:      %s\n", tree_space, tree_right, kstr);

    }


  g_print ("\n");
}

static int
authorize (BoltClient *client, int argc, char **argv)
{
  g_autoptr(GOptionContext) optctx = NULL;
  g_autoptr(BoltDevice) dev = NULL;
  g_autoptr(GError) error = NULL;
  const char *uid;
  gboolean ok;

  optctx = g_option_context_new ("DEVICE");
  g_option_context_set_summary (optctx, "Authorize a thunderbolt device");
  g_option_context_set_strict_posix (optctx, TRUE);

  if (!g_option_context_parse (optctx, &argc, &argv, &error))
    {
      g_printerr ("Failed to parse command line arguments.\n");
      return EXIT_FAILURE;
    }

  if (argc < 2)
    {
      char *help = g_option_context_get_help (optctx, TRUE, NULL);
      g_printerr ("%s\n", help);
      return EXIT_FAILURE;
    }

  uid = argv[1];

  dev = bolt_client_get_device (client, uid, &error);
  if (dev == NULL)
    {
      g_printerr ("%s\n", error->message);
      return EXIT_FAILURE;
    }

  ok = bolt_device_authorize (dev, &error);
  if (!ok)
    g_printerr ("Authorization error: %s\n", error->message);

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}


static int
enroll (BoltClient *client, int argc, char **argv)
{
  g_autoptr(GOptionContext) optctx = NULL;
  g_autoptr(BoltDevice) dev = NULL;
  g_autoptr(GError) error = NULL;
  const char *uid;

  optctx = g_option_context_new ("DEVICE");
  g_option_context_set_summary (optctx, "Information about a thunderbolt device");
  g_option_context_set_strict_posix (optctx, TRUE);

  if (!g_option_context_parse (optctx, &argc, &argv, &error))
    {
      g_printerr ("Failed to parse command line arguments.\n");
      return EXIT_FAILURE;
    }

  if (argc < 2)
    {
      char *help = g_option_context_get_help (optctx, TRUE, NULL);
      g_printerr ("%s\n", help);
      return EXIT_FAILURE;
    }

  uid = argv[1];

  dev = bolt_client_enroll_device (client, uid, BOLT_POLICY_DEFAULT, &error);
  if (dev == NULL)
    {
      g_printerr ("%s\n", error->message);
      return EXIT_FAILURE;
    }

  print_device (dev, TRUE);
  return EXIT_SUCCESS;
}

static int
forget (BoltClient *client, int argc, char **argv)
{
  g_autoptr(GOptionContext) optctx = NULL;
  g_autoptr(BoltDevice) dev = NULL;
  g_autoptr(GError) error = NULL;
  const char *uid;
  gboolean ok;

  optctx = g_option_context_new ("DEVICE");
  g_option_context_set_summary (optctx, "Forget a thunderbolt device");
  g_option_context_set_strict_posix (optctx, TRUE);

  if (!g_option_context_parse (optctx, &argc, &argv, &error))
    {
      g_printerr ("Failed to parse command line arguments.\n");
      return EXIT_FAILURE;
    }

  if (argc < 2)
    {
      char *help = g_option_context_get_help (optctx, TRUE, NULL);
      g_printerr ("%s\n", help);
      return EXIT_FAILURE;
    }

  uid = argv[1];

  dev = bolt_client_get_device (client, uid, &error);
  if (dev == NULL)
    {
      g_printerr ("%s\n", error->message);
      return EXIT_FAILURE;
    }

  ok = bolt_device_forget (dev, &error);
  if (!ok)
    g_printerr ("Failed to forget device: %s\n", error->message);

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}


static int
info (BoltClient *client, int argc, char **argv)
{
  g_autoptr(GOptionContext) optctx = NULL;
  g_autoptr(BoltDevice) dev = NULL;
  g_autoptr(GError) error = NULL;
  const char *uid;

  optctx = g_option_context_new ("DEVICE");
  g_option_context_set_summary (optctx, "Information about a thunderbolt device");
  g_option_context_set_strict_posix (optctx, TRUE);

  if (!g_option_context_parse (optctx, &argc, &argv, &error))
    {
      g_printerr ("Failed to parse command line arguments.\n");
      return EXIT_FAILURE;
    }

  if (argc < 2)
    {
      char *help = g_option_context_get_help (optctx, TRUE, NULL);
      g_printerr ("%s\n", help);
      return EXIT_FAILURE;
    }

  uid = argv[1];

  dev = bolt_client_get_device (client, uid, &error);
  if (dev == NULL)
    {
      g_printerr ("%s\n", error->message);
      return EXIT_FAILURE;
    }

  print_device (dev, TRUE);
  return EXIT_SUCCESS;
}

static void
handle_device_added (BoltClient *cli,
                     BoltDevice *dev,
                     gpointer    user_data)
{
  g_autofree char *path = NULL;

  g_object_get (dev,
                "object-path", &path,
                NULL);

  g_print (" DeviceAdded: %s\n", path);
}

static void
handle_device_removed (BoltClient *cli,
                       const char *opath,
                       gpointer    user_data)
{
  g_print (" DeviceRemoved: %s\n", opath);
}

static int
monitor (BoltClient *client, int argc, char **argv)
{
  g_autofree char *version = NULL;

  g_autoptr(GMainLoop) main_loop = NULL;

  g_object_get (client, "version", &version, NULL);
  g_print ("Daemon Version: %s\n", version);
  g_print ("Ready\n");

  main_loop = g_main_loop_new (NULL, FALSE);

  g_signal_connect (client, "device-added",
                    G_CALLBACK (handle_device_added), NULL);

  g_signal_connect (client, "device-removed",
                    G_CALLBACK (handle_device_removed), NULL);

  g_main_loop_run (main_loop);

  return EXIT_SUCCESS;
}


static int
list_devices (BoltClient *client, int argc, char **argv)
{
  g_autoptr(GPtrArray) devices = NULL;
  g_autoptr(GError) error = NULL;

  devices = bolt_client_list_devices (client, &error);
  if (devices == NULL)
    {
      g_printerr ("Failed to list devices: %s",
                  error->message);
      return EXIT_FAILURE;
    }

  for (guint i = 0; i < devices->len; i++)
    {
      BoltDevice *dev = g_ptr_array_index (devices, i);
      print_device (dev, FALSE);
    }

  return EXIT_SUCCESS;
}

/* ****  */

typedef int (*run_t)(BoltClient *client,
                     int         argc,
                     char      **argv);

typedef struct SubCommand
{
  const char *name;
  run_t       fn;
} SubCommand;

static SubCommand subcommands[] = {
  {"authorize",    authorize},
  {"enroll",       enroll},
  {"forget",       forget},
  {"info",         info},
  {"list",         list_devices},
  {"monitor",      monitor}
};

int
main (int argc, char **argv)
{
  g_autoptr(GOptionContext) optctx = NULL;
  g_autoptr(BoltClient) client = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *cmdline = NULL;
  SubCommand *cmd = NULL;

  setlocale (LC_ALL, "");

  optctx = g_option_context_new (NULL);

  g_option_context_set_summary (optctx, "Manage thunderbolt devices");
  g_option_context_set_strict_posix (optctx, TRUE);

  if (!g_option_context_parse (optctx, &argc, &argv, &error))
    {
      g_printerr ("%s: %s", g_get_application_name (), error->message);
      g_printerr ("\n");
      g_printerr ("Try \"%s --help\" for more information.",
                  g_get_prgname ());
      g_printerr ("\n");

      return EXIT_FAILURE;
    }

  if (argc < 2)
    {
      g_printerr ("Usage %s COMMAND\n", argv[0]);
      for (size_t i = 0; i < G_N_ELEMENTS (subcommands); i++)
        g_printerr (" %s\n", subcommands[i].name);

      return EXIT_FAILURE;
    }

  /* register the back-mapping from to our error */
  (void) BOLT_ERROR;

  client = bolt_client_new (&error);

  if (!client)
    {
      g_error ("Could not create client: %s", error->message);
      return EXIT_FAILURE;
    }

  for (size_t i = 0; !cmd && i < G_N_ELEMENTS (subcommands); i++)
    if (g_str_equal (argv[1], subcommands[i].name))
      cmd = &subcommands[i];

  if (cmd == NULL)
    {
      g_printerr ("Unknown subcommand: %s", argv[1]);
      return EXIT_FAILURE;
    }

  cmdline = g_strconcat (g_get_prgname (),
                         " ",
                         cmd->name,
                         NULL);

  argv[1] = cmdline;
  argv += 1;
  argc -= 1;

  return cmd->fn (client, argc, argv);
}

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

#include <gio/gio.h>

#include <locale.h>
#include <stdlib.h>


static void
print_device (BoltDevice *dev)
{
  g_autofree char *path = NULL;
  g_autofree char *uid = NULL;
  g_autofree char *name = NULL;
  g_autofree char *vendor = NULL;
  BoltSecurity security = BOLT_SECURITY_NONE;

  g_object_get (dev,
                "object-path", &path,
                "name", &name,
                "vendor", &vendor,
                "uid", &uid,
                "security", &security,
                NULL);

  g_print (" %s\n", name);
  g_print ("   uid: %s\n", uid);
  g_print ("   dbus object path: %s\n", path);
  g_print ("   vendor: %s\n", vendor);
  g_print ("   security: %s\n",
           bolt_security_to_string (security));

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
      print_device (dev);
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

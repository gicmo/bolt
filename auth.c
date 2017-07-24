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
#include "manager.h"

static gboolean
write_char (GFile *loc, char data, GError **error)
{
  g_autofree char *path = NULL;
  ssize_t n;
  int fd;
  int r;

  path = g_file_get_path (loc);

  fd = open (path, O_WRONLY);
  if (fd == -1)
    {
      int errsv        = errno;
      char buffer[255] = {
        0,
      };
      strerror_r (errsv, buffer, sizeof (buffer));

      g_set_error (error, G_IO_ERROR, errsv, "Could not open file (%s): %s", path, buffer);
    }

retry:
  n = write (fd, &data, 1);

  if (n == -1)
    {
      int errsv = errno;
      if (errsv == EINTR)
        {
          goto retry;
        }
      else
        {
          char buffer[255] = {
            0,
          };
          strerror_r (errsv, buffer, sizeof (buffer));

          g_set_error (error, G_IO_ERROR, errsv, "Could not write data: %s", buffer);
        }
    }

  r = close (fd);
  if (n > 0 && r != 0)
    {
      int errsv = errno;
      g_set_error_literal (error, G_IO_ERROR, errsv, "Could not close file.");
    }

  return n > 0;
}

static GFile *
tb_device_authfile (TbDevice *dev)
{
  g_autoptr(GFile) base = NULL;
  GFile *r;

  base = g_file_new_for_path (dev->sysfs);
  r    = g_file_get_child (base, "authorized");
  return r;
}

static gboolean
tb_device_authorize (TbManager *mgr, TbDevice *dev, GError **error)
{
  g_autoptr(GFile) authloc = NULL;

  authloc = tb_device_authfile (dev);

  return write_char (authloc, '1', error);
}

static gboolean do_store = FALSE;
static gboolean do_auto  = FALSE;
static GOptionEntry authorize_opts[] =
{{"store", 's', 0, G_OPTION_ARG_NONE, &do_store, "Store device", NULL},
 {"auto", 'a', 0, G_OPTION_ARG_NONE, &do_auto, "Auto-authorize device (implies --store)", NULL},
 {NULL}};

static int
authorize_device (TbManager *mgr, int argc, char **argv)
{
  g_autoptr(GError) error          = NULL;
  g_autoptr(GOptionContext) optctx = NULL;
  g_autoptr(TbDevice) dev          = NULL;
  const char *uid;
  gboolean ok;

  optctx = g_option_context_new (NULL);

  g_option_context_set_summary (optctx, "Authorize a specific thunderbolt device");
  g_option_context_set_strict_posix (optctx, TRUE);
  g_option_context_add_main_entries (optctx, authorize_opts, NULL);

  if (!g_option_context_parse (optctx, &argc, &argv, &error))
    {
      g_printerr ("Failed to parse command line arguments.");
      return EXIT_FAILURE;
    }

  if (argc < 2)
    {
      g_printerr ("Need device id\n");
      return EXIT_FAILURE;
    }

  uid = argv[1];

  dev = tb_manager_lookup (mgr, uid);
  if (dev == NULL)
    {
      g_printerr ("Could not find device\n");
      return EXIT_FAILURE;
    }

  ok = tb_device_authorize (mgr, dev, &error);

  if (!ok)
    {
      g_printerr ("Could not authorize device: %s [%d]\n", error->message, error->code);
      return EXIT_FAILURE;
    }

  if (do_auto)
    {
      do_store = TRUE;

      dev->autoconnect = TRUE;
    }

  if (do_store)
    {
      ok = tb_manager_store (mgr, dev, &error);
      if (!ok)
        {
          g_fprintf (stderr, "Could not store device in database: %s\n", error->message);
          return EXIT_FAILURE;
        }
    }

  return EXIT_SUCCESS;
}

static int
auto_device (TbManager *mgr, int argc, char **argv)
{
  g_autoptr(GError) error          = NULL;
  g_autoptr(GFile) authloc         = NULL;
  g_autoptr(GOptionContext) optctx = NULL;
  g_autoptr(TbDevice) dev          = NULL;
  const char *uid;
  gboolean ok;

  optctx = g_option_context_new (NULL);

  g_option_context_set_summary (optctx, "Automatically authorize thunderbolt devices");
  g_option_context_set_strict_posix (optctx, TRUE);

  if (!g_option_context_parse (optctx, &argc, &argv, &error))
    {
      g_printerr ("Failed to parse command line arguments.");
      return EXIT_FAILURE;
    }

  if (argc < 2)
    {
      g_printerr ("Need device id\n");
      return EXIT_FAILURE;
    }

  uid = argv[1];

  dev = tb_manager_lookup (mgr, uid);
  if (dev == NULL)
    {
      g_printerr ("Could not find device\n");
      return EXIT_FAILURE;
    }

  if (!tb_device_in_store (dev))
    {
      g_print ("thunderbolt device %s not in store.", tb_device_get_uid (dev));
      return EXIT_SUCCESS;
    }
  else if (!tb_device_autoconnect (dev))
    {
      g_print ("thunderbolt device %s not setup for auto authorization.", tb_device_get_uid (dev));
      return EXIT_SUCCESS;
    }

  authloc = tb_device_authfile (dev);

  ok = write_char (authloc, '1', &error);

  if (!ok)
    {
      g_printerr ("Could not authorize device: %s [%d]\n", error->message, error->code);
      return EXIT_FAILURE;
    }

  return EXIT_SUCCESS;
}

static char **
make_args (int argc, char **argv)
{
  char **args = g_new (char *, argc - 1);
  int i;

  args[0] = g_strdup (argv[0]);

  for (i              = 1; i < argc; i++)
    args[i - 1] = g_strdup (argv[i]);

  return args;
}

int
main (int argc, char **argv)
{
  g_autoptr(GError) error          = NULL;
  g_autoptr(TbManager) mgr         = NULL;
  g_autoptr(GOptionContext) optctx = NULL;
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

  mgr = tb_manager_new (&error);

  if (g_str_equal (cmd, "authorize"))
    {
      g_auto(GStrv) args = make_args (argc, argv);
      res                = authorize_device (mgr, argc - 1, args);
    }
  else if (g_str_equal (cmd, "auto"))
    {
      g_auto(GStrv) args = make_args (argc, argv);
      res                = auto_device (mgr, argc - 1, args);
    }
  else
    {
      g_printerr ("Unknown command.\n");
    }

  return res;
}

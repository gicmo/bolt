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

#include "bolt-client.h"
#include "bolt-enums.h"
#include "bolt-error.h"
#include "bolt-str.h"
#include "bolt-term.h"
#include "bolt-time.h"

#include <gio/gio.h>

#include <locale.h>
#include <stdlib.h>

int
usage_error (GError *error)
{
  g_printerr ("%s:", g_get_application_name ());
  g_printerr ("%s error: %s", bolt_color (ANSI_RED), bolt_color (ANSI_NORMAL));
  if (error)
    g_printerr ("%s", error->message);
  g_printerr ("\n");
  g_printerr ("Try \"%s --help\" for more information.", g_get_prgname ());
  g_printerr ("\n");

  return EXIT_FAILURE;
}

int
usage_error_need_arg (const char *arg)
{
  g_autoptr(GError) error = NULL;

  g_set_error (&error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
               "missing argument '%s'", arg);
  return usage_error (error);
}

int
usage_error_too_many_args (void)
{
  g_autoptr(GError) error = NULL;

  g_set_error (&error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
               "too many arguments");
  return usage_error (error);
}

/* domain related commands */
static void
print_domain (BoltDomain *domain, gboolean verbose)
{
  const char *tree_branch;
  const char *tree_right;
  const char *id;
  const char *syspath;
  const char *security;
  BoltSecurity sl;

  tree_branch = bolt_glyph (TREE_BRANCH);
  tree_right = bolt_glyph (TREE_RIGHT);

  id = bolt_domain_get_id (domain);
  sl = bolt_domain_get_security (domain);

  syspath = bolt_domain_get_syspath (domain);
  security = bolt_security_to_string (sl);

  g_print (" %s\n", id);
  if (verbose)
    g_print ("   %s syspath:       %s\n", tree_branch, syspath);
  g_print ("   %s security:      %s\n", tree_right, security);

  g_print ("\n");
}

static int
list_domains (BoltClient *client, int argc, char **argv)
{
  g_autoptr(GOptionContext) optctx = NULL;
  g_autoptr(GError) err = NULL;
  g_autoptr(GPtrArray) domains = NULL;
  gboolean details = FALSE;
  GOptionEntry options[] = {
    { "verbose", 'v', 0, G_OPTION_ARG_NONE, &details, "Show more details", NULL },
    { NULL }
  };

  optctx = g_option_context_new ("- List thunderbolt domains");
  g_option_context_add_main_entries (optctx, options, NULL);

  if (!g_option_context_parse (optctx, &argc, &argv, &err))
    return usage_error (err);

  domains = bolt_client_list_domains (client, NULL, &err);

  if (domains == NULL)
    {
      g_warning ("Could not list domains: %s", err->message);
      domains = g_ptr_array_new_with_free_func (g_object_unref);
    }

  for (guint i = 0; i < domains->len; i++)
    {
      BoltDomain *dom = g_ptr_array_index (domains, i);
      print_domain (dom, details);
    }

  return EXIT_SUCCESS;
}

/* device related commands */
static gboolean
format_timestamp (BoltDevice *dev,
                  char       *buffer,
                  gsize       n,
                  const char *timesel)
{
  g_autofree char *str = NULL;
  guint64 ts;

  g_object_get (dev, timesel, &ts, NULL);

  if (ts > 0)
    {
      str = bolt_epoch_format (ts, "%c");
      g_utf8_strncpy (buffer, str, n);
    }
  else
    {
      g_utf8_strncpy (buffer, "no", n);
    }

  return ts > 0;
}

void
print_device (BoltDevice *dev, gboolean verbose)
{
  const char *path;
  const char *uid;
  const char *name;
  const char *vendor;
  const char *syspath;
  const char *parent;
  const char *label;
  BoltDeviceType type;
  BoltStatus status;
  BoltAuthFlags aflags;
  BoltKeyState keystate;
  BoltPolicy policy;
  const char *status_color;
  const char *status_symbol;
  const char *type_text;
  const char *status_text;
  const char *tree_branch;
  const char *tree_right;
  const char *tree_space;
  gboolean stored;
  gboolean pcie;
  char buf[256];

  path = g_dbus_proxy_get_object_path (G_DBUS_PROXY (dev));
  uid = bolt_device_get_uid (dev);
  name = bolt_device_get_name (dev);
  vendor = bolt_device_get_vendor (dev);
  type = bolt_device_get_device_type (dev);
  status = bolt_device_get_status (dev);
  aflags = bolt_device_get_authflags (dev);
  parent = bolt_device_get_parent (dev);
  syspath = bolt_device_get_syspath (dev);
  stored = bolt_device_is_stored (dev);
  policy = bolt_device_get_policy (dev);
  keystate = bolt_device_get_keystate (dev);

  pcie = bolt_flag_isclear (aflags, BOLT_AUTH_NOPCIE);

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

    case BOLT_STATUS_CONNECTING:
      status_color = bolt_color (ANSI_YELLOW);
      status_text = "connecting";
      break;

    case BOLT_STATUS_CONNECTED:
      status_color = bolt_color (ANSI_YELLOW);
      status_text = "connected";
      break;

    case BOLT_STATUS_AUTHORIZED:
    case BOLT_STATUS_AUTHORIZED_NEWKEY:
    case BOLT_STATUS_AUTHORIZED_SECURE:
    case BOLT_STATUS_AUTHORIZED_DPONLY:
      if (pcie)
        {
          status_color = bolt_color (ANSI_GREEN);
          status_text = "authorized";
        }
      else
        {
          status_color = bolt_color (ANSI_BLUE);
          status_text = "connected (no PCIe tunnels)";
        }
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

  label = bolt_device_get_display_name (dev);

  g_print (" %s%s%s %s\n",
           status_color,
           status_symbol,
           bolt_color (ANSI_NORMAL),
           label ? : name);

  type_text = bolt_device_type_to_string (type);

  g_print ("   %s type:          %s\n", tree_branch, type_text);
  g_print ("   %s name:          %s\n", tree_branch, name);
  g_print ("   %s vendor:        %s\n", tree_branch, vendor);
  g_print ("   %s uuid:          %s\n", tree_branch, uid);
  if (verbose)
    g_print ("   %s dbus path:     %s\n", tree_branch, path);
  g_print ("   %s status:        %s\n", tree_branch, status_text);

  if (bolt_status_is_connected (status))
    {
      g_autofree char *flags = NULL;
      const char *domain;

      domain = bolt_device_get_domain (dev);
      g_print ("   %s %s domain:     %s\n",
               bolt_glyph (TREE_VERTICAL),
               tree_branch,
               domain);

      if (verbose)
        {
          g_print ("   %s %s parent:     %s\n",
                   bolt_glyph (TREE_VERTICAL),
                   tree_branch,
                   parent);
          g_print ("   %s %s syspath:    %s\n",
                   bolt_glyph (TREE_VERTICAL),
                   tree_branch,
                   syspath);
        }

      flags = bolt_flags_to_string (BOLT_TYPE_AUTH_FLAGS, aflags, NULL);
      g_print ("   %s %s authflags:  %s\n",
               bolt_glyph (TREE_VERTICAL),
               tree_right,
               flags);
    }

  if (format_timestamp (dev, buf, sizeof (buf), "authtime"))
    g_print ("   %s authorized:    %s\n", tree_branch, buf);

  if (format_timestamp (dev, buf, sizeof (buf), "conntime"))
    g_print ("   %s connected:     %s\n", tree_branch, buf);

  format_timestamp (dev, buf, sizeof (buf), "storetime");
  g_print ("   %s stored:        %s\n", tree_right, buf);

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

      g_print ("   %s %s policy:     %s\n", tree_space, tree_branch, pstr);
      g_print ("   %s %s key:        %s\n", tree_space, tree_right, kstr);

    }

  g_print ("\n");
}

static int
info (BoltClient *client, int argc, char **argv)
{
  g_autoptr(GOptionContext) optctx = NULL;
  g_autoptr(BoltDevice) dev = NULL;
  g_autoptr(GError) error = NULL;
  const char *uid;

  optctx = g_option_context_new ("DEVICE - Show information about a device");

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

  print_device (dev, TRUE);
  return EXIT_SUCCESS;
}

static int
list_devices (BoltClient *client, int argc, char **argv)
{
  g_autoptr(GOptionContext) optctx = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GPtrArray) devices = NULL;
  gboolean show_all = FALSE;
  GOptionEntry options[] = {
    { "all", 'a', 0, G_OPTION_ARG_NONE, &show_all, "Show all devices", NULL },
    { NULL }
  };

  optctx = g_option_context_new ("- List thunderbolt devices");
  g_option_context_add_main_entries (optctx, options, NULL);

  if (!g_option_context_parse (optctx, &argc, &argv, &error))
    return usage_error (error);

  devices = bolt_client_list_devices (client, NULL, &error);
  if (devices == NULL)
    {
      g_printerr ("Failed to list devices: %s",
                  error->message);
      return EXIT_FAILURE;
    }

  bolt_devices_sort_by_syspath (devices, FALSE);
  for (guint i = 0; i < devices->len; i++)
    {
      BoltDevice *dev = g_ptr_array_index (devices, i);
      BoltDeviceType type;

      if (!show_all)
        {
          type = bolt_device_get_device_type (dev);
          if (type != BOLT_DEVICE_PERIPHERAL)
            continue;
        }

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
  const char *desc;
} SubCommand;

static SubCommand subcommands[] = {
  {"authorize",    authorize,     "Authorize a device"},
  {"enroll",       enroll,        "Authorize and store a device in the database"},
  {"domains",      list_domains,  "List the active thunderbolt domains"},
  {"forget",       forget,        "Remove a stored device from the database"},
  {"info",         info,          "Show information about a device"},
  {"list",         list_devices,  "List connected and stored devices"},
  {"monitor",      monitor,       "Listen and print changes"},
  {"power",        power,         "Force power configuration of the controller"}
};

#define SUMMARY_SPACING 17
static void
option_context_make_summary (GOptionContext *ctx)
{
  g_autoptr(GString) s = NULL;

  s = g_string_new ("Commands:");

  for (size_t i = 0; i < G_N_ELEMENTS (subcommands); i++)
    {
      const SubCommand *c = &subcommands[i];
      int spacing = SUMMARY_SPACING - strlen (c->name);
      g_string_append_printf (s, "\n  %s", c->name);
      g_string_append_printf (s, "%*s%s", spacing, "", c->desc);
    }

  g_option_context_set_summary (ctx, s->str);
}

int
main (int argc, char **argv)
{
  g_autoptr(GOptionContext) optctx = NULL;
  g_autoptr(BoltClient) client = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *cmdline = NULL;
  SubCommand *cmd = NULL;
  const char *cmdname = NULL;
  gboolean version = FALSE;
  GOptionEntry options[] = {
    { "version", 0, 0, G_OPTION_ARG_NONE, &version, "Print version information and exit", NULL },
    { NULL }
  };

  setlocale (LC_ALL, "");

  optctx = g_option_context_new ("[COMMAND]");
  g_option_context_add_main_entries (optctx, options, NULL);

  option_context_make_summary (optctx);
  g_option_context_set_strict_posix (optctx, TRUE);

  if (!g_option_context_parse (optctx, &argc, &argv, &error))
    return usage_error (error);

  if (version)
    {
      g_print ("%s %s\n", PACKAGE_NAME, PACKAGE_VERSION);
      exit (EXIT_SUCCESS);
    }

  if (argc < 2)
    cmdname = "list";
  else
    cmdname = argv[1];

  client = bolt_client_new (&error);

  if (!client)
    {
      g_error ("Could not create client: %s", error->message);
      return EXIT_FAILURE;
    }

  for (size_t i = 0; !cmd && i < G_N_ELEMENTS (subcommands); i++)
    if (g_str_equal (cmdname, subcommands[i].name))
      cmd = &subcommands[i];

  if (cmd == NULL)
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "Invalid command: %s", cmdname);
      return usage_error (error);
    }

  cmdline = g_strconcat (g_get_prgname (),
                         " ",
                         cmd->name,
                         NULL);

  g_set_prgname (cmdline);
  argv[1] = cmdline;
  argv += 1;
  argc -= 1;

  return cmd->fn (client, argc, argv);
}

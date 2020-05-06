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
#include "boltctl-uidfmt.h"

#include "bolt-client.h"
#include "bolt-enums.h"
#include "bolt-error.h"
#include "bolt-macros.h"
#include "bolt-str.h"
#include "bolt-term.h"
#include "bolt-time.h"

#include <gio/gio.h>

#include <locale.h>
#include <stdlib.h>

gboolean
check_argc (int      argc,
            int      lower,
            int      upper,
            GError **error)
{
  argc--; /* discard argv[0] */

  if (lower == upper && argc != upper)
    {
      g_set_error (error, BOLT_ERROR, BOLT_ERROR_FAILED,
                   "unexpected number of arguments: %d, wanted %d",
                   argc, upper);
      return FALSE;
    }
  else if (argc < lower)
    {
      g_set_error (error, BOLT_ERROR, BOLT_ERROR_FAILED,
                   "not enough arguments: %d, wanted at least %d",
                   argc, lower);
      return FALSE;
    }
  else if (argc > upper)
    {
      g_set_error (error, BOLT_ERROR, BOLT_ERROR_FAILED,
                   "too many arguments: %d, wanted at most %d",
                   argc, lower);
      return FALSE;
    }

  return TRUE;
}

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

int
report_error (const char *prefix, GError *error)
{
  g_printerr ("%s:", g_get_application_name ());
  g_printerr ("%s error: %s", bolt_color (ANSI_RED), bolt_color (ANSI_NORMAL));

  if (prefix)
    g_printerr ("%s", prefix);

  if (error)
    {
      if (g_dbus_error_is_remote_error (error))
        g_dbus_error_strip_remote_error (error);

      if (prefix)
        g_printerr (": ");

      g_printerr ("%s", error->message);
    }

  g_printerr ("\n");

  return EXIT_FAILURE;
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

static gboolean
format_generation (guint generation, char *buffer, size_t n)
{
  switch (generation)
    {
    case 1:
    case 2:
    case 3:
      g_snprintf (buffer, n, "Thunderbolt %u", generation);
      return TRUE;

    case 4:
      g_snprintf (buffer, n, "USB4");
      return TRUE;

    case 0:
      g_snprintf (buffer, n, "Unknown");
      return FALSE;

    default:
      g_snprintf (buffer, n, "%u", generation);
      return TRUE;
    }
}

void
print_device (BoltDevice *dev, gboolean verbose)
{
  g_autofree char *label = NULL;
  const char *path;
  const char *uid;
  const char *name;
  const char *vendor;
  const char *syspath;
  const char *parent;
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
  guint gen;

  path = g_dbus_proxy_get_object_path (G_DBUS_PROXY (dev));
  uid = bolt_device_get_uid (dev);
  name = bolt_device_get_name (dev);
  vendor = bolt_device_get_vendor (dev);
  gen = bolt_device_get_generation (dev);
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
  g_print ("   %s uuid:          %s\n", tree_branch, format_uid (uid));
  if (verbose)
    g_print ("   %s dbus path:     %s\n", tree_branch, path);
  if (format_generation (gen, buf, sizeof (buf)) || verbose)
    g_print ("   %s generation:    %s\n", tree_branch, buf);
  g_print ("   %s status:        %s\n", tree_branch, status_text);

  if (bolt_status_is_connected (status))
    {
      g_autofree char *flags = NULL;
      const char *domain;
      BoltLinkSpeed speed = {0, };

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

      bolt_device_get_linkspeed (dev, &speed);
      if (speed.rx.lanes && speed.rx.speed)
        {
          g_print ("   %s %s rx speed:   %u Gb/s = %u lanes * %u Gb/s\n",
                   bolt_glyph (TREE_VERTICAL),
                   tree_branch,
                   speed.rx.lanes * speed.rx.speed,
                   speed.rx.lanes, speed.rx.speed);
        }

      if (speed.tx.lanes && speed.tx.speed)
        {
          g_print ("   %s %s tx speed:   %u Gb/s = %u lanes * %u Gb/s\n",
                   bolt_glyph (TREE_VERTICAL),
                   tree_branch,
                   speed.tx.lanes * speed.tx.speed,
                   speed.tx.lanes, speed.tx.speed);
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


/* ****  */

static SubCommand subcommands[] = {
  {"authorize",    authorize,     "Authorize a device"},
  {"config",       config,        "Get or set global, device or domain properties"},
  {"domains",      list_domains,  "List the active thunderbolt domains"},
  {"enroll",       enroll,        "Authorize and store a device in the database"},
  {"forget",       forget,        "Remove a stored device from the database"},
  {"info",         info,          "Show information about a device"},
  {"list",         list_devices,  "List connected and stored devices"},
  {"monitor",      monitor,       "Listen and print changes"},
  {"power",        power,         "Force power configuration of the controller"},
  {NULL,           NULL,          NULL},
};

char *
subcommands_make_summary (const SubCommand *cmds)
{
  GString *s = g_string_new ("Commands:");
  unsigned int spacing = 0;

  for (const SubCommand *c = cmds; c && c->name; c++)
    spacing = MAX (spacing, strlen (c->name));

  spacing = MAX (15, spacing) + 2;

  for (const SubCommand *c = cmds; c && c->name; c++)
    {
      int space = spacing - strlen (c->name);
      g_string_append_printf (s, "\n  %s", c->name);
      g_string_append_printf (s, "%*s%s", space, "", c->desc);
    }

  return g_string_free (s, FALSE);
}

const SubCommand *
subcommands_find (const SubCommand *cmds,
                  const char       *cmdname,
                  GError          **error)
{
  const SubCommand *cmd = NULL;

  for (const SubCommand *c = cmds; !cmd && c && c->name; c++)
    if (g_str_equal (cmdname, c->name))
      cmd = c;

  if (cmd == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "Invalid command: %s", cmdname);
      return NULL;
    }

  return cmd;
}

int
subcommand_run (const SubCommand *cmd,
                BoltClient       *client,
                int               argc,
                char            **argv)
{
  g_autofree char *cmdline = NULL;
  g_autofree char **args = NULL;
  int count;

  cmdline = g_strconcat (g_get_prgname (),
                         " ",
                         cmd->name,
                         NULL);

  g_set_prgname (cmdline);

  count = MAX (argc - 1, 1);
  args = g_new0 (char *, count + 1);

  args[0] = cmdline;
  for (int i = 1; i < count; i++)
    args[i] = argv[i + 1];

  return cmd->fn (client, count, args);
}

int
main (int argc, char **argv)
{
  g_autoptr(GOptionContext) optctx = NULL;
  g_autoptr(BoltClient) client = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *summary = NULL;
  const SubCommand *cmd = NULL;
  const char *cmdname = NULL;
  const char *uuid_fmtstr = "full";
  gboolean version = FALSE;
  int fmt = -1;
  GOptionEntry options[] = {
    { "version", 0, 0, G_OPTION_ARG_NONE, &version, "Print version information and exit", NULL },
    { "uuids", 'U', 0, G_OPTION_ARG_STRING, &uuid_fmtstr, "How to format uuids [*full, short, alias]", NULL },
    { NULL }
  };

  setlocale (LC_ALL, "");

  optctx = g_option_context_new ("[COMMAND]");
  g_option_context_add_main_entries (optctx, options, NULL);

  summary = subcommands_make_summary (subcommands);
  g_option_context_set_summary (optctx, summary);
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

  fmt = format_uid_init (uuid_fmtstr, &error);
  if (fmt == -1)
    return usage_error (error);

  client = bolt_client_new (&error);

  if (!client)
    return report_error ("could not create client", error);

  cmd = subcommands_find (subcommands, cmdname, &error);

  if (cmd == NULL)
    return usage_error (error);

  return subcommand_run (cmd, client, argc, argv);
}

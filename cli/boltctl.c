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
#include "bolt-str.h"
#include "bolt-term.h"
#include "bolt-time.h"

#include <gio/gio.h>

#include <locale.h>
#include <stdlib.h>

static int
usage_error (GError *error)
{
  g_printerr ("%s:", g_get_application_name ());
  g_printerr ("%s error: %s", bolt_color (ANSI_RED), bolt_color (ANSI_NORMAL));
  g_printerr ("%s", error->message);
  g_printerr ("\n");
  g_printerr ("Try \"%s --help\" for more information.", g_get_prgname ());
  g_printerr ("\n");

  return EXIT_FAILURE;
}

static int
usage_error_need_arg (const char *arg)
{
  g_autoptr(GError) error = NULL;

  g_set_error (&error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
               "missing argument '%s'", arg);
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
static void
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
  guint64 ct, at, st;

  path = g_dbus_proxy_get_object_path (G_DBUS_PROXY (dev));
  uid = bolt_device_get_uid (dev);
  name = bolt_device_get_name (dev);
  vendor = bolt_device_get_vendor (dev);
  type = bolt_device_get_device_type (dev);
  status = bolt_device_get_status (dev);
  aflags = bolt_device_get_authflags (dev);
  parent = bolt_device_get_parent (dev);
  syspath = bolt_device_get_syspath (dev);
  ct = bolt_device_get_conntime (dev);
  at = bolt_device_get_authtime (dev);
  stored = bolt_device_is_stored (dev);
  policy = bolt_device_get_policy (dev);
  keystate = bolt_device_get_keystate (dev);
  st = bolt_device_get_storetime (dev);

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
      g_autofree char *ctstr = NULL;
      g_autofree char *flags = NULL;
      const char *domain;

      domain = bolt_device_get_domain (dev);
      g_print ("   %s %s domain:     %s\n",
               bolt_glyph (TREE_VERTICAL),
               tree_branch,
               domain);

      flags = bolt_flags_to_string (BOLT_TYPE_AUTH_FLAGS, aflags, NULL);
      g_print ("   %s %s authflags:  %s\n",
               bolt_glyph (TREE_VERTICAL),
               tree_branch,
               flags);

      ctstr = bolt_epoch_format (ct, "%c");

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

      if (bolt_status_is_authorized (status))
        {
          g_autofree char *atstr = NULL;

          atstr = bolt_epoch_format (at, "%c");
          g_print ("   %s %s authorized: %s\n",
                   bolt_glyph (TREE_VERTICAL),
                   tree_branch,
                   atstr);
        }

      g_print ("   %s %s connected:  %s\n",
               bolt_glyph (TREE_VERTICAL),
               tree_right,
               ctstr);

    }

  g_print ("   %s stored:        %s\n", tree_right, bolt_yesno (stored));

  if (stored)
    {
      g_autofree char *etstr = NULL;
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

      etstr = bolt_epoch_format (st, "%c");

      g_print ("   %s %s when:       %s\n", tree_space, tree_branch, etstr);
      g_print ("   %s %s policy:     %s\n", tree_space, tree_branch, pstr);
      g_print ("   %s %s key:        %s\n", tree_space, tree_right, kstr);

    }

  g_print ("\n");
}

static int
authorize (BoltClient *client, int argc, char **argv)
{
  g_autoptr(GOptionContext) optctx = NULL;
  g_autoptr(BoltDevice) dev = NULL;
  g_autoptr(GError) error = NULL;
  BoltAuthCtrl flags = BOLT_AUTHCTRL_NONE;
  const char *uid;
  gboolean ok;

  optctx = g_option_context_new ("DEVICE - Authorize a device");

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

  ok = bolt_device_authorize (dev, flags, NULL, &error);
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

static int
forget (BoltClient *client, int argc, char **argv)
{
  g_autoptr(GOptionContext) optctx = NULL;
  g_autoptr(GError) error = NULL;
  const char *uid;
  gboolean ok;

  optctx = g_option_context_new ("DEVICE - Remove a device form the store");

  if (!g_option_context_parse (optctx, &argc, &argv, &error))
    return usage_error (error);

  if (argc < 2)
    return usage_error_need_arg ("DEVICE");

  uid = argv[1];

  ok = bolt_client_forget_device (client, uid, &error);
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


static void
handle_domain_added (BoltClient *cli,
                     const char *opath,
                     gpointer    user_data)
{
  g_autoptr(GError) err = NULL;
  GDBusConnection *bus;
  BoltDomain *dom;
  GPtrArray *domains = user_data;

  bus = g_dbus_proxy_get_connection (G_DBUS_PROXY (cli));
  dom = bolt_domain_new_for_object_path (bus, opath, NULL, &err);

  if (err != NULL)
    {
      g_warning ("Could not create proxy object for %s", opath);
      return;
    }


  g_print (" DomainAdded: %s\n", opath);

  g_ptr_array_add (domains, dom);
}

static void
handle_domain_removed (BoltClient *cli,
                       const char *opath,
                       gpointer    user_data)
{
  GPtrArray *domains = user_data;
  BoltDomain *domain = NULL;

  for (guint i = 0; i < domains->len; i++)
    {
      BoltDomain *dom = g_ptr_array_index (domains, i);
      const char *dom_opath = g_dbus_proxy_get_object_path (G_DBUS_PROXY (dom));

      if (bolt_streq (opath, dom_opath))
        {
          domain = dom;
          break;
        }
    }

  if (domain == NULL)
    {
      g_warning ("DomainRemoved signal for unknown domain: %s", opath);
      return;
    }

  g_print (" DomainRemoved: %s\n", opath);

  g_ptr_array_remove_fast (domains, domain);
}

static void
handle_device_changed (GObject    *gobject,
                       GParamSpec *pspec,
                       gpointer    user_data)
{
  const char *uid = NULL;
  const char *dev_name = NULL;
  g_autofree char *val = NULL;

  BoltDevice *dev = BOLT_DEVICE (gobject);
  const char *prop_name;
  GValue prop_val =  G_VALUE_INIT;
  GValue str_val = G_VALUE_INIT;

  uid = bolt_device_get_uid (dev);
  dev_name = bolt_device_get_name (dev);

  prop_name = g_param_spec_get_name (pspec);

  g_value_init (&prop_val, G_PARAM_SPEC_VALUE_TYPE (pspec));
  g_value_init (&str_val, G_TYPE_STRING);

  g_object_get_property (G_OBJECT (dev), prop_name, &prop_val);
  g_print ("[%s] %30s | %10s -> ", uid, dev_name, prop_name);

  if (g_value_transform (&prop_val, &str_val))
    val = g_value_dup_string (&str_val);
  else
    val = g_strdup_value_contents (&prop_val);

  g_print ("%s\n", val ? : "");

  g_value_unset (&str_val);
  g_value_unset (&prop_val);
}

static void
handle_device_added (BoltClient *cli,
                     const char *opath,
                     gpointer    user_data)
{
  g_autoptr(GError) err = NULL;
  GDBusConnection *bus;
  BoltDevice *dev;
  GPtrArray *devices = user_data;

  g_print (" DeviceAdded: %s\n", opath);

  bus = g_dbus_proxy_get_connection (G_DBUS_PROXY (cli));
  dev = bolt_device_new_for_object_path (bus, opath, NULL, &err);

  if (err != NULL)
    {
      g_warning ("Could not create proxy object for %s", opath);
      return;
    }

  g_ptr_array_add (devices, dev);
  g_signal_connect (dev, "notify",
                    G_CALLBACK (handle_device_changed),
                    NULL);
}

static void
handle_device_removed (BoltClient *cli,
                       const char *opath,
                       gpointer    user_data)
{
  GPtrArray *devices = user_data;
  BoltDevice *device = NULL;

  g_print (" DeviceRemoved: %s\n", opath);

  for (guint i = 0; i < devices->len; i++)
    {
      BoltDevice *dev = g_ptr_array_index (devices, i);
      const char *dev_opath = g_dbus_proxy_get_object_path (G_DBUS_PROXY (dev));

      if (bolt_streq (opath, dev_opath))
        {
          device = dev;
          break;
        }
    }

  if (device == NULL)
    {
      g_warning ("DeviceRemoved signal for unknown device: %s", opath);
      return;
    }

  g_signal_handlers_block_by_func (device, handle_device_changed, devices);
  g_ptr_array_remove_fast (devices, device);
}

static void
handle_probing_changed (BoltClient *client,
                        GParamSpec *pspec,
                        gpointer    user_data)
{
  gboolean probing = bolt_client_is_probing (client);

  if (probing)
    g_print ("Probing started\n");
  else
    g_print ("Probing done\n");
}

static int
monitor (BoltClient *client, int argc, char **argv)
{
  g_autoptr(GOptionContext) optctx = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GMainLoop) main_loop = NULL;
  g_autoptr(GPtrArray) devices = NULL;
  g_autoptr(GPtrArray) domains = NULL;
  g_autofree char *amstr = NULL;
  BoltSecurity security;
  BoltAuthMode authmode;
  guint version = 0;

  optctx = g_option_context_new ("- Watch for changes");

  if (!g_option_context_parse (optctx, &argc, &argv, &error))
    return usage_error (error);

  version = bolt_client_get_version (client);
  security = bolt_client_get_security (client);
  authmode = bolt_client_get_authmode (client);
  amstr = bolt_flags_to_string (BOLT_TYPE_AUTH_MODE, authmode, NULL);

  if (!bolt_proxy_has_name_owner (BOLT_PROXY (client)))
    g_print ("%s no name owner for bolt (not running?)\n",
             bolt_glyph (WARNING_SIGN));

  g_print ("Bolt Version  : %d.%d\n", VERSION_MAJOR, VERSION_MINOR);
  g_print ("Daemon API    : %u\n", version);
  g_print ("Client API    : %u\n", BOLT_DBUS_API_VERSION);
  g_print ("Security Level: %s\n", bolt_security_to_string (security));
  g_print ("Auth Mode     : %s\n", amstr);
  g_print ("Ready\n");

  /* domains */
  domains = bolt_client_list_domains (client, NULL, &error);

  if (domains == NULL)
    {
      g_warning ("Could not list domains: %s", error->message);
      devices = g_ptr_array_new_with_free_func (g_object_unref);
      g_clear_error (&error);
    }

  g_signal_connect (client, "domain-added",
                    G_CALLBACK (handle_domain_added), domains);

  g_signal_connect (client, "domain-removed",
                    G_CALLBACK (handle_domain_removed), domains);


  /* devices */
  devices = bolt_client_list_devices (client, NULL, &error);

  if (devices == NULL)
    {
      g_warning ("Could not list devices: %s", error->message);
      devices = g_ptr_array_new_with_free_func (g_object_unref);
      g_clear_error (&error);
    }

  bolt_devices_sort_by_syspath (devices, FALSE);
  for (guint i = 0; i < devices->len; i++)
    {
      BoltDevice *dev = g_ptr_array_index (devices, i);
      g_signal_connect (dev, "notify",
                        G_CALLBACK (handle_device_changed),
                        NULL);
    }

  g_signal_connect (client, "device-added",
                    G_CALLBACK (handle_device_added), devices);

  g_signal_connect (client, "device-removed",
                    G_CALLBACK (handle_device_removed), devices);

  g_signal_connect (client, "notify::probing",
                    G_CALLBACK (handle_probing_changed), NULL);

  main_loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (main_loop);

  g_signal_handlers_disconnect_by_func (client,
                                        G_CALLBACK (handle_probing_changed),
                                        NULL);

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
  {"monitor",      monitor,       "Listen and print changes"}
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

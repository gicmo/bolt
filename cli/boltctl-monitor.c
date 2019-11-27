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

  g_print ("%" G_GINT64_FORMAT " ", g_get_real_time ());

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
  const char *name;
  const char *uid;

  bus = g_dbus_proxy_get_connection (G_DBUS_PROXY (cli));
  dev = bolt_device_new_for_object_path (bus, opath, NULL, &err);

  if (err != NULL)
    {
      g_warning ("Could not create proxy object for %s", opath);
      return;
    }

  uid = bolt_device_get_uid (dev);
  name = bolt_device_get_name (dev);

  g_print ("%" G_GINT64_FORMAT " ", g_get_real_time ());
  g_print ("[%s] %30s | DeviceAdded @ %s\n", uid, name, opath);

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
  const char *name;
  const char *uid;

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

  uid = bolt_device_get_uid (device);
  name = bolt_device_get_name (device);

  g_print ("%" G_GINT64_FORMAT " ", g_get_real_time ());
  g_print ("[%s] %30s | DeviceRemoved @ %s\n", uid, name, opath);

  g_signal_handlers_block_by_func (device, handle_device_changed, devices);
  g_ptr_array_remove_fast (devices, device);
}

static void
handle_probing_changed (BoltClient *client,
                        GParamSpec *pspec,
                        gpointer    user_data)
{
  gboolean probing = bolt_client_is_probing (client);

  g_print ("%" G_GINT64_FORMAT " ", g_get_real_time ());

  if (probing)
    g_print ("Probing started\n");
  else
    g_print ("Probing done\n");
}

int
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
      domains = g_ptr_array_new_with_free_func (g_object_unref);
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

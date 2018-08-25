/*
 * Copyright © 2018 Red Hat, Inc
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

#include "bolt-power.h"

#include <gio/gio.h>

struct _BoltPower
{
  BoltProxy parent;
};

enum {
  PROP_0,

  /* D-Bus Props */
  PROP_SUPPORTED,
  PROP_STATE,
  PROP_TIMEOUT,

  PROP_LAST
};

static GParamSpec *props[PROP_LAST] = {NULL, };

G_DEFINE_TYPE (BoltPower,
               bolt_power,
               BOLT_TYPE_PROXY);

static void
bolt_power_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  if (bolt_proxy_get_dbus_property (object, pspec, value))
    return;
}



static void
bolt_power_class_init (BoltPowerClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = bolt_power_get_property;

  props[PROP_SUPPORTED] =
    g_param_spec_boolean ("supported",
                          "Supported", NULL,
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_NICK);

  props[PROP_STATE] =
    g_param_spec_enum ("state",
                       "State", NULL,
                       BOLT_TYPE_POWER_STATE,
                       BOLT_FORCE_POWER_UNSET,
                       G_PARAM_READABLE |
                       G_PARAM_STATIC_STRINGS);

  props[PROP_TIMEOUT] =
    g_param_spec_uint ("timeout",
                       "Timeout", NULL,
                       0, G_MAXINT, 0,
                       G_PARAM_READABLE |
                       G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     props);

}

static void
bolt_power_init (BoltPower *power)
{
}

/* public methods */
BoltPower *
bolt_power_new_for_object_path (GDBusConnection *bus,
                                GCancellable    *cancel,
                                GError         **error)
{
  BoltPower *pwr;

  pwr = g_initable_new (BOLT_TYPE_POWER,
                        cancel, error,
                        "g-flags", G_DBUS_PROXY_FLAGS_NONE,
                        "g-connection", bus,
                        "g-name", BOLT_DBUS_NAME,
                        "g-object-path", BOLT_DBUS_PATH,
                        "g-interface-name", BOLT_DBUS_POWER_INTERFACE,
                        NULL);

  return pwr;
}

gboolean
bolt_power_is_supported (BoltPower *power)
{
  const char *key;
  gboolean val = FALSE;
  gboolean ok;

  g_return_val_if_fail (BOLT_IS_POWER (power), val);

  key = g_param_spec_get_name (props[PROP_SUPPORTED]);
  ok = bolt_proxy_get_property_bool (BOLT_PROXY (power), key, &val);

  if (!ok)
    g_warning ("failed to get enum property '%s'", key);

  return val;
}

BoltPowerState
bolt_power_get_state (BoltPower *power)
{
  const char *key;
  gboolean ok;
  gint val = BOLT_FORCE_POWER_UNSET;

  g_return_val_if_fail (BOLT_IS_POWER (power), val);

  key = g_param_spec_get_name (props[PROP_STATE]);
  ok = bolt_proxy_get_property_enum (BOLT_PROXY (power), key, &val);

  if (!ok)
    g_warning ("failed to get enum property '%s'", key);

  return val;
}

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

#include "bolt-proxy.h"

#include "bolt-dbus.h"
#include "bolt-error.h"

static void bolt_proxy_handle_props_changed (GDBusProxy *proxy,
                                             GVariant   *changed_properties,
                                             GStrv       invalidated_properties,
                                             gpointer    user_data);

static void bolt_proxy_handle_dbus_signal (GDBusProxy  *proxy,
                                           const gchar *sender_name,
                                           const gchar *signal_name,
                                           GVariant    *params,
                                           gpointer     user_data);
enum {
  PROP_0,

  PROP_BUS,

  /* d-bus names */
  PROP_OBJECT_PATH,
  PROP_IFACE_NAME,

  PROP_LAST
};

G_DEFINE_TYPE (BoltProxy, bolt_proxy, G_TYPE_DBUS_PROXY);


static void
bolt_proxy_constructed (GObject *object)
{
  G_OBJECT_CLASS (bolt_proxy_parent_class)->constructed (object);

  g_signal_connect (object, "g-properties-changed",
                    G_CALLBACK (bolt_proxy_handle_props_changed), object);

  g_signal_connect (object, "g-signal",
                    G_CALLBACK (bolt_proxy_handle_dbus_signal), object);
}

static const BoltProxyProp *
bolt_proxy_get_dbus_props (guint *n)
{
  *n = 0;
  return NULL;
}

static const BoltProxySignal *
bolt_proxy_get_dbus_signals (guint *n)
{
  *n = 0;
  return NULL;
}

static void
bolt_proxy_class_init (BoltProxyClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->constructed = bolt_proxy_constructed;

  klass->get_dbus_props = bolt_proxy_get_dbus_props;
  klass->get_dbus_signals = bolt_proxy_get_dbus_signals;

}

static void
bolt_proxy_init (BoltProxy *object)
{
}

static void
bolt_proxy_handle_props_changed (GDBusProxy *proxy,
                                 GVariant   *changed_properties,
                                 GStrv       invalidated_properties,
                                 gpointer    user_data)
{
  gboolean handled = FALSE;
  GVariantIter *iter;
  const BoltProxyProp *pp;
  const char *key;
  guint n;

  pp = BOLT_PROXY_GET_CLASS (proxy)->get_dbus_props (&n);

  g_variant_get (changed_properties, "a{sv}", &iter);
  while (g_variant_iter_next (iter, "{&sv}", &key, NULL))
    {
      for (guint i = 0; !handled && i < n; i++)
        {
          const BoltProxyProp *prop = &pp[i];
          const char *name = prop->theirs;

          if (!g_str_equal (key, name))
            continue;

          g_object_notify (G_OBJECT (user_data), prop->ours);
          handled = TRUE;
        }
    }
}


static void
bolt_proxy_handle_dbus_signal (GDBusProxy  *proxy,
                               const gchar *sender_name,
                               const gchar *signal_name,
                               GVariant    *params,
                               gpointer     user_data)
{
  const BoltProxySignal *ps;
  guint n;

  if (signal_name == NULL)
    return;

  ps = BOLT_PROXY_GET_CLASS (proxy)->get_dbus_signals (&n);

  for (guint i = 0; i < n; i++)
    {
      const BoltProxySignal *sig = &ps[i];

      if (g_str_equal (sig->theirs, signal_name))
        {
          sig->handle (G_OBJECT (proxy), proxy, params);
          break;
        }
    }

}

/* public methods */

gboolean
bolt_proxy_get_dbus_property (GObject *proxy,
                              guint    prop_id,
                              GValue  *value)
{
  gboolean handled = FALSE;
  const BoltProxyProp *pp;
  guint n;

  pp = BOLT_PROXY_GET_CLASS (proxy)->get_dbus_props (&n);

  for (guint i = 0; !handled && i < n; i++)
    {
      g_autoptr(GVariant) val = NULL;
      const BoltProxyProp *prop = &pp[i];
      const char *name = prop->theirs;

      if (prop->prop_id != prop_id)
        continue;

      handled = TRUE;

      val = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (proxy), name);

      if (val == NULL)
        break;

      if (prop->convert == NULL)
        g_dbus_gvariant_to_gvalue (val, value);
      else
        prop->convert (val, value);
    }

  return handled;
}

const char *
bolt_proxy_get_object_path (BoltProxy *proxy)
{
  return g_dbus_proxy_get_object_path (G_DBUS_PROXY (proxy));
}

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

#include "bolt-error.h"
#include "bolt-names.h"
#include "bolt-str.h"

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
  g_autoptr(GVariantIter) iter = NULL;
  gboolean handled = FALSE;
  GParamSpec **pp;
  const char *key;
  guint n;

  pp = g_object_class_list_properties (G_OBJECT_GET_CLASS (proxy), &n);

  g_variant_get (changed_properties, "a{sv}", &iter);
  while (g_variant_iter_next (iter, "{&sv}", &key, NULL))
    {
      for (guint i = 0; !handled && i < n; i++)
        {
          GParamSpec *pspec = pp[i];
          const char *nick;
          const char *name;

          nick = g_param_spec_get_nick (pspec);
          name = g_param_spec_get_name (pspec);

          handled = bolt_streq (nick, key);

          if (handled)
            g_object_notify (G_OBJECT (user_data), name);
        }
    }

  g_free (pp);
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
bolt_proxy_get_dbus_property (GObject    *proxy,
                              GParamSpec *spec,
                              GValue     *value)
{
  g_autoptr(GVariant) val = NULL;
  const GVariantType *vt;
  gboolean handled = FALSE;
  const char *nick;

  nick = g_param_spec_get_nick (spec);
  val = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (proxy), nick);

  if (val == NULL)
    return FALSE;

  vt = g_variant_get_type (val);

  if (g_variant_type_equal (vt, G_VARIANT_TYPE_STRING) &&
      G_IS_PARAM_SPEC_ENUM (spec))
    {
      GParamSpecEnum *enum_spec = G_PARAM_SPEC_ENUM (spec);
      GEnumValue *ev;
      const char *str;

      str = g_variant_get_string (val, NULL);
      ev = g_enum_get_value_by_nick (enum_spec->enum_class, str);

      handled = ev != NULL;

      if (handled)
        g_value_set_enum (value, ev->value);
      else
        g_value_set_enum (value, enum_spec->default_value);
    }
  else
    {
      g_dbus_gvariant_to_gvalue (val, value);
    }

  return handled;
}

const char *
bolt_proxy_get_object_path (BoltProxy *proxy)
{
  return g_dbus_proxy_get_object_path (G_DBUS_PROXY (proxy));
}

static GParamSpec *
find_property (BoltProxy  *proxy,
               const char *name,
               GError    **error)
{
  GParamSpec *res = NULL;
  GParamSpec **pp;
  guint n;

  pp = g_object_class_list_properties (G_OBJECT_GET_CLASS (proxy), &n);

  for (guint i = 0; i < n; i++)
    {
      GParamSpec *pspec = pp[i];

      if (bolt_streq (pspec->name, name))
        {
          res = pspec;
          break;
        }
    }

  if (pp == NULL)
    g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
                 "could not find property '%s'", name);

  g_free (pp);
  return res;
}

gboolean
bolt_proxy_set_property (BoltProxy    *proxy,
                         const char   *name,
                         GVariant     *value,
                         GCancellable *cancellable,
                         GError      **error)
{
  GParamSpec *pp;
  const char *iface;
  gboolean ok = FALSE;
  GVariant *res;

  pp = find_property (proxy, name, NULL);
  if (pp != NULL)
    name = g_param_spec_get_nick (pp);

  iface = g_dbus_proxy_get_interface_name (G_DBUS_PROXY (proxy));

  res = g_dbus_proxy_call_sync (G_DBUS_PROXY (proxy),
                                "org.freedesktop.DBus.Properties.Set",
                                g_variant_new ("(ssv)",
                                               iface,
                                               name,
                                               value),
                                G_DBUS_CALL_FLAGS_NONE,
                                -1,
                                cancellable,
                                error);

  if (res)
    {
      g_variant_unref (res);
      ok = TRUE;
    }

  return ok;
}

void
bolt_proxy_set_property_async (BoltProxy          *proxy,
                               const char         *name,
                               GVariant           *value,
                               GCancellable       *cancellable,
                               GAsyncReadyCallback callback,
                               gpointer            user_data)
{
  GParamSpec *pp;
  const char *iface;

  pp = find_property (proxy, name, NULL);

  if (pp != NULL)
    name = g_param_spec_get_nick (pp);

  iface = g_dbus_proxy_get_interface_name (G_DBUS_PROXY (proxy));

  g_dbus_proxy_call (G_DBUS_PROXY (proxy),
                     "org.freedesktop.DBus.Properties.Set",
                     g_variant_new ("(ssv)",
                                    iface,
                                    name,
                                    value),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     cancellable,
                     callback,
                     user_data);
}

gboolean
bolt_proxy_set_property_finish (GAsyncResult *res,
                                GError      **error)
{
  BoltProxy *proxy;
  GVariant *val = NULL;

  proxy = (BoltProxy *) g_async_result_get_source_object (res);
  val = g_dbus_proxy_call_finish (G_DBUS_PROXY (proxy), res, error);

  if (val == NULL)
    return FALSE;

  g_variant_unref (val);
  return TRUE;
}

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
#include "bolt-enums.h"
#include "bolt-error.h"
#include "bolt-glue.h"
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

struct _BoltProxyClassPrivate
{
  GHashTable *wire_convs;
};

static gpointer bolt_proxy_parent_class = NULL;

static void     bolt_proxy_init (GTypeInstance *,
                                 gpointer g_class);
static void     bolt_proxy_class_init (BoltProxyClass *klass);
static void     bolt_proxy_base_init (gpointer g_class);
static void     bolt_proxy_base_finalize (gpointer g_class);

GType
bolt_proxy_get_type (void)
{
  static volatile gsize proxy_type = 0;

  if (g_once_init_enter (&proxy_type))
    {
      GType type_id;
      const GTypeInfo type_info = {
        sizeof (BoltProxyClass),
        bolt_proxy_base_init,
        (GBaseFinalizeFunc) bolt_proxy_base_finalize,
        (GClassInitFunc) bolt_proxy_class_init,
        NULL,           /* class_finalize */
        NULL,           /* class_data */
        sizeof (BoltProxy),
        0,              /* n_preallocs */
        bolt_proxy_init,
        NULL,           /* value_table */
      };

      type_id = g_type_register_static (G_TYPE_DBUS_PROXY, "BoltProxy",
                                        &type_info, G_TYPE_FLAG_ABSTRACT);
      g_type_add_class_private (type_id, sizeof (BoltProxyClassPrivate));
      g_once_init_leave (&proxy_type, type_id);
    }

  return proxy_type;
}


static void
bolt_proxy_constructed (GObject *object)
{
  g_autoptr(GError) err = NULL;
  GDBusInterfaceInfo *info;
  const char *interface;

  G_OBJECT_CLASS (bolt_proxy_parent_class)->constructed (object);

  g_signal_connect (object, "g-properties-changed",
                    G_CALLBACK (bolt_proxy_handle_props_changed), object);

  g_signal_connect (object, "g-signal",
                    G_CALLBACK (bolt_proxy_handle_dbus_signal), object);

  if (g_dbus_proxy_get_interface_info (G_DBUS_PROXY (object)) != NULL)
    return;

  interface = g_dbus_proxy_get_interface_name (G_DBUS_PROXY (object));

  if (interface == NULL)
    return;

  bolt_dbus_ensure_resources ();

  info = bolt_dbus_interface_info_lookup (BOLT_DBUS_GRESOURCE_PATH,
                                          interface,
                                          &err);

  if (info == NULL)
    {
      g_warning ("could not load interface info: %s", err->message);
      return;
    }

  g_dbus_proxy_set_interface_info (G_DBUS_PROXY (object), info);
  g_dbus_interface_info_unref (info);
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

  bolt_proxy_parent_class = g_type_class_peek_parent (klass);

  gobject_class->constructed = bolt_proxy_constructed;

  klass->get_dbus_signals = bolt_proxy_get_dbus_signals;

}

static void
bolt_proxy_base_init (gpointer g_class)
{
  BoltProxyClass *klass = g_class;

  klass->priv = G_TYPE_CLASS_GET_PRIVATE (g_class, BOLT_TYPE_PROXY, BoltProxyClassPrivate);
  memset (klass->priv, 0, sizeof (BoltProxyClassPrivate));

  klass->priv->wire_convs = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                   NULL,
                                                   (GDestroyNotify) bolt_wire_conv_unref);
}

static void
bolt_proxy_base_finalize (gpointer g_class)
{
  BoltProxyClass *klass = g_class;
  BoltProxyClassPrivate *priv = klass->priv;

  g_hash_table_unref (priv->wire_convs);
}

static void
bolt_proxy_init (GTypeInstance *instance, gpointer g_class)
{
}

static void
bolt_proxy_handle_props_changed (GDBusProxy *proxy,
                                 GVariant   *changed_properties,
                                 GStrv       invalidated_properties,
                                 gpointer    user_data)
{
  g_autoptr(GVariantIter) iter = NULL;
  gboolean handled;
  GParamSpec **pp;
  const char *key;
  guint n;

  pp = g_object_class_list_properties (G_OBJECT_GET_CLASS (proxy), &n);

  g_variant_get (changed_properties, "a{sv}", &iter);
  while (g_variant_iter_next (iter, "{&sv}", &key, NULL))
    {
      handled = FALSE;
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

static BoltWireConv *
bolt_proxy_get_wire_conv (BoltProxy  *proxy,
                          GParamSpec *spec,
                          GError    **error)
{
  GDBusInterfaceInfo *info;
  GDBusPropertyInfo *pi;
  BoltProxyClass *klass;
  const char *nick;
  BoltWireConv *conv;

  klass = BOLT_PROXY_GET_CLASS (proxy);

  nick = g_param_spec_get_nick (spec);
  conv = g_hash_table_lookup (klass->priv->wire_convs, nick);

  if (conv)
    return conv;

  info = g_dbus_proxy_get_interface_info (G_DBUS_PROXY (proxy));

  if (info == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "could not find dbus interface info");
      return NULL;
    }

  pi = g_dbus_interface_info_lookup_property (info, nick);
  if (pi == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "could not find dbus property info");
      return NULL;
    }

  conv = bolt_wire_conv_for (G_VARIANT_TYPE (pi->signature), spec);

  if (conv == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "could create conversion helper");
      return NULL;
    }

  /* nick is valid as long as spec is valid and
   * a reference to spec is held by conv */
  g_hash_table_insert (klass->priv->wire_convs,
                       (gpointer) nick, conv);

  return conv;
}

/* public methods */
void
bolt_proxy_property_getter (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *spec)
{
  bolt_proxy_get_dbus_property (BOLT_PROXY (object),
                                spec,
                                value);
}

gboolean
bolt_proxy_get_dbus_property (BoltProxy  *proxy,
                              GParamSpec *spec,
                              GValue     *value)
{
  g_autoptr(GVariant) val = NULL;
  g_autoptr(GError) err = NULL;
  BoltWireConv *conv;
  const char *nick;
  gboolean ok;

  nick = g_param_spec_get_nick (spec);
  val = g_dbus_proxy_get_cached_property (G_DBUS_PROXY (proxy), nick);

  if (val == NULL)
    {
      const GValue *def = g_param_spec_get_default_value (spec);

      if (G_VALUE_TYPE (value) == 0)
        g_value_init (value, spec->value_type);
      g_value_copy (def, value);

      g_warning ("Unknown property: %s (%s)", spec->name, nick);

      return FALSE;
    }

  conv = bolt_proxy_get_wire_conv (proxy, spec, &err);

  if (conv == NULL)
    {
      g_warning ("No conversion available for dbus property '%s': %s",
                 nick, err->message);
      return FALSE;
    }

  ok = bolt_wire_conv_from_wire (conv, val, value, &err);

  if (!ok)
    g_warning ("Failed to convert dbus property '%s': %s",
               nick, err->message);

  return ok;
}

const char *
bolt_proxy_get_object_path (BoltProxy *proxy)
{
  return g_dbus_proxy_get_object_path (G_DBUS_PROXY (proxy));
}

gboolean
bolt_proxy_has_name_owner (BoltProxy *proxy)
{
  const char *name_owner;

  g_return_val_if_fail (proxy != NULL, FALSE);
  g_return_val_if_fail (BOLT_IS_PROXY (proxy), FALSE);

  name_owner = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (proxy));

  return name_owner != NULL;
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
bolt_proxy_get_bool_by_pspec (gpointer    object,
                              GParamSpec *spec)
{
  g_auto(GValue) val = G_VALUE_INIT;
  BoltProxy *proxy = object;

  g_return_val_if_fail (BOLT_IS_PROXY (proxy), FALSE);

  bolt_proxy_get_dbus_property (proxy, spec, &val);

  return g_value_get_boolean (&val);
}


gint
bolt_proxy_get_enum_by_pspec (gpointer    object,
                              GParamSpec *spec)
{
  g_auto(GValue) val = G_VALUE_INIT;
  BoltProxy *proxy = (BoltProxy *) object;

  g_return_val_if_fail (BOLT_IS_PROXY (proxy), FALSE);

  bolt_proxy_get_dbus_property (proxy, spec, &val);

  return g_value_get_enum (&val);
}

guint
bolt_proxy_get_flags_by_pspec (gpointer    object,
                               GParamSpec *spec)
{
  g_auto(GValue) val = G_VALUE_INIT;
  BoltProxy *proxy = (BoltProxy *) object;

  g_return_val_if_fail (BOLT_IS_PROXY (proxy), FALSE);

  bolt_proxy_get_dbus_property (proxy, spec, &val);

  return g_value_get_flags (&val);
}

guint32
bolt_proxy_get_uint32_by_pspec (gpointer    object,
                                GParamSpec *spec)
{
  g_auto(GValue) val = G_VALUE_INIT;
  BoltProxy *proxy = (BoltProxy *) object;

  g_return_val_if_fail (BOLT_IS_PROXY (proxy), FALSE);

  bolt_proxy_get_dbus_property (proxy, spec, &val);

  return g_value_get_uint (&val);
}

gint64
bolt_proxy_get_int64_by_pspec (gpointer    object,
                               GParamSpec *spec)
{
  g_auto(GValue) val = G_VALUE_INIT;
  BoltProxy *proxy = (BoltProxy *) object;

  g_return_val_if_fail (BOLT_IS_PROXY (proxy), FALSE);

  bolt_proxy_get_dbus_property (proxy, spec, &val);

  return g_value_get_int64 (&val);
}

guint64
bolt_proxy_get_uint64_by_pspec (gpointer    object,
                                GParamSpec *spec)
{
  g_auto(GValue) val = G_VALUE_INIT;
  BoltProxy *proxy = (BoltProxy *) object;

  g_return_val_if_fail (BOLT_IS_PROXY (proxy), FALSE);

  bolt_proxy_get_dbus_property (proxy, spec, &val);

  return g_value_get_uint64 (&val);
}

const char *
bolt_proxy_get_string_by_pspec (gpointer    object,
                                GParamSpec *spec)
{
  g_auto(GValue) val = G_VALUE_INIT;
  BoltProxy *proxy = (BoltProxy *) object;

  g_return_val_if_fail (BOLT_IS_PROXY (proxy), FALSE);

  bolt_proxy_get_dbus_property (proxy, spec, &val);

  /* NB: Yes, this is return the string inside the value,
   * although the value is freed. This therefore makes
   * the assumption that the GValue's value was initialized
   * via a static string (g_value_set_static_string). Ergo,
   * this must always be true for bolt_proxy_get_dbus_property
   */
  return g_value_get_string (&val);
}

char **
bolt_proxy_get_strv_by_pspec (gpointer    object,
                              GParamSpec *spec)
{
  g_auto(GValue) val = G_VALUE_INIT;
  BoltProxy *proxy = (BoltProxy *) object;

  g_return_val_if_fail (BOLT_IS_PROXY (proxy), FALSE);

  bolt_proxy_get_dbus_property (proxy, spec, &val);

  return (char **) g_value_dup_boxed (&val);
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

gboolean
bolt_proxy_set (BoltProxy    *proxy,
                GParamSpec   *spec,
                const GValue *value,
                GCancellable *cancellable,
                GError      **error)
{
  g_autoptr(GVariant) val = NULL;
  g_autoptr(GVariant) res = NULL;
  BoltWireConv *conv;
  const char *name;
  const char *iface;

  g_return_val_if_fail (BOLT_IS_PROXY (proxy), FALSE);
  g_return_val_if_fail (G_IS_PARAM_SPEC (spec), FALSE);
  g_return_val_if_fail (G_IS_VALUE (value), FALSE);
  g_return_val_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable), FALSE);
  g_return_val_if_fail (error != NULL || *error == NULL, FALSE);

  conv = bolt_proxy_get_wire_conv (proxy, spec, error);

  if (conv == NULL)
    return FALSE;

  val = bolt_wire_conv_to_wire (conv, value, error);

  if (val == NULL)
    return FALSE;

  name = g_param_spec_get_nick (spec);
  iface = g_dbus_proxy_get_interface_name (G_DBUS_PROXY (proxy));

  res = g_dbus_proxy_call_sync (G_DBUS_PROXY (proxy),
                                "org.freedesktop.DBus.Properties.Set",
                                g_variant_new ("(ssv)",
                                               iface,
                                               name,
                                               val),
                                G_DBUS_CALL_FLAGS_NONE,
                                -1,
                                cancellable,
                                error);

  return res != NULL;
}

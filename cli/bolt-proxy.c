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


static gboolean bolt_proxy_initable_init (GInitable    *initable,
                                          GCancellable *cancellable,
                                          GError      **error);
static void     bolt_proxy_initable_iface_init (GInitableIface *iface);


struct _BoltProxyPrivate
{

  GDBusProxy      *proxy;

  char            *object_path;
  char            *iface_name;

  GDBusConnection *bus;
};


enum {
  PROP_0,

  PROP_BUS,

  /* d-bus names */
  PROP_OBJECT_PATH,
  PROP_IFACE_NAME,

  PROP_LAST
};

static GParamSpec *props[PROP_LAST] = {NULL, };

#define GET_PRIV(obj) (bolt_proxy_get_instance_private (BOLT_PROXY (obj)))

G_DEFINE_TYPE_WITH_CODE (BoltProxy,
                         bolt_proxy,
                         G_TYPE_OBJECT,
                         G_ADD_PRIVATE (BoltProxy)
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                bolt_proxy_initable_iface_init));

static void
bolt_proxy_finalize (GObject *object)
{
  BoltProxyPrivate *priv = GET_PRIV (object);

  if (priv->bus)
    g_clear_object (&priv->bus);

  if (priv->proxy)
    g_clear_object (&priv->proxy);

  g_free (priv->object_path);
  g_free (priv->iface_name);

  G_OBJECT_CLASS (bolt_proxy_parent_class)->finalize (object);
}

static void
bolt_proxy_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  BoltProxyPrivate *self = GET_PRIV (object);

  switch (prop_id)
    {
    case PROP_BUS:
      g_value_set_object (value, self->bus);
      break;

    case PROP_OBJECT_PATH:
      g_value_set_string (value, self->object_path);
      break;

    case PROP_IFACE_NAME:
      g_value_set_string (value, self->iface_name);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bolt_proxy_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  BoltProxyPrivate *self = GET_PRIV (object);

  switch (prop_id)
    {
    case PROP_BUS:
      self->bus = g_value_dup_object (value);
      break;

    case PROP_OBJECT_PATH:
      self->object_path = g_value_dup_string (value);
      break;

    case PROP_IFACE_NAME:
      self->iface_name = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
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

  gobject_class->finalize = bolt_proxy_finalize;

  gobject_class->get_property = bolt_proxy_get_property;
  gobject_class->set_property = bolt_proxy_set_property;

  klass->get_dbus_props = bolt_proxy_get_dbus_props;
  klass->get_dbus_signals = bolt_proxy_get_dbus_signals;

  props[PROP_BUS] =
    g_param_spec_object ("bus",
                         NULL, NULL,
                         G_TYPE_DBUS_CONNECTION,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_NICK);

  props[PROP_OBJECT_PATH] =
    g_param_spec_string ("object-path",
                         NULL, NULL,
                         NULL,
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_NICK);

  props[PROP_IFACE_NAME] =
    g_param_spec_string ("interface-name",
                         NULL, NULL,
                         NULL,
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_NICK);

  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     props);

}

static void
bolt_proxy_initable_iface_init (GInitableIface *iface)
{
  iface->init = bolt_proxy_initable_init;
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

  pp = BOLT_PROXY_GET_CLASS (user_data)->get_dbus_props (&n);

  g_printerr ("properties changed!");

  g_variant_get (changed_properties, "a{sv}", &iter);
  while (g_variant_iter_next (iter, "{&sv}", &key, NULL))
    {
      for (guint i = 0; handled && i < n; i++)
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
bolt_proxy_handle_dbus_signal (GDBusProxy  *bus_proxy,
                               const gchar *sender_name,
                               const gchar *signal_name,
                               GVariant    *params,
                               BoltProxy   *proxy)
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
          sig->handle (G_OBJECT (proxy), bus_proxy, params);
          break;
        }
    }

}


static gboolean
bolt_proxy_initable_init (GInitable    *initable,
                          GCancellable *cancellable,
                          GError      **error)
{
  BoltProxyPrivate *self = GET_PRIV (initable);

  if (self->bus == NULL)
    {
      g_set_error (error, BOLT_ERROR, BOLT_ERROR_FAILED,
                   "Need dbus connection");
      return FALSE;
    }

  if (self->object_path == NULL || self->iface_name == NULL)
    {
      g_set_error (error, BOLT_ERROR, BOLT_ERROR_FAILED,
                   "Need object and interface names");
      return FALSE;
    }

  /* register the back-mapping from to our error */
  (void) BOLT_ERROR;

  self->proxy = g_dbus_proxy_new_sync (self->bus,
                                       G_DBUS_PROXY_FLAGS_NONE,
                                       NULL,
                                       BOLT_DBUS_NAME,
                                       self->object_path,
                                       self->iface_name,
                                       NULL,
                                       error);
  if (self->proxy == NULL)
    return FALSE;

  g_signal_connect (self->proxy, "g-properties-changed",
                    G_CALLBACK (bolt_proxy_handle_props_changed), initable);

  g_signal_connect (self->proxy, "g-signal",
                    G_CALLBACK (bolt_proxy_handle_dbus_signal), initable);

  return TRUE;
}

/* public methods */

gboolean
bolt_proxy_get_dbus_property (GObject *proxy,
                              guint    prop_id,
                              GValue  *value)
{
  BoltProxyPrivate *self = GET_PRIV (proxy);
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

      val = g_dbus_proxy_get_cached_property (self->proxy, name);

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
  BoltProxyPrivate *self = GET_PRIV (proxy);

  return self->object_path;
}

GDBusProxy *
bolt_proxy_get_proxy (BoltProxy *proxy)
{
  BoltProxyPrivate *self = GET_PRIV (proxy);

  return self->proxy;
}

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

#pragma once

#include <gio/gio.h>
#include <bolt-glue.h>

G_BEGIN_DECLS

typedef struct BoltProxySignal
{

  const char *theirs;
  void (*handle)(GObject    *self,
                 GDBusProxy *bus_proxy,
                 GVariant   *params);

} BoltProxySignal;

#define BOLT_TYPE_PROXY (bolt_proxy_get_type ())
G_DECLARE_DERIVABLE_TYPE (BoltProxy, bolt_proxy, BOLT, PROXY, GDBusProxy)

typedef struct _BoltProxyClassPrivate BoltProxyClassPrivate;

struct _BoltProxyClass
{
  GDBusProxyClass parent;

  /*< private >*/
  BoltProxyClassPrivate *priv;

  /*< public >*/

  /* virtuals */
  const BoltProxySignal * (*get_dbus_signals) (guint *n);

  /* for the future */
  gpointer padding[10];
};

gboolean          bolt_proxy_set_wireconv (BoltProxy       *proxy,
                                           GParamSpec      *param_spec,
                                           const char      *custom_id,
                                           BoltConvToWire   to_wire,
                                           BoltConvFromWire from_wire,
                                           GError         **error);

void              bolt_proxy_property_getter (GObject    *object,
                                              guint       prop_id,
                                              GValue     *value,
                                              GParamSpec *spec);

gboolean          bolt_proxy_get_dbus_property (BoltProxy  *proxy,
                                                GParamSpec *spec,
                                                GValue     *value);

gboolean          bolt_proxy_has_name_owner (BoltProxy *proxy);

const char *      bolt_proxy_get_object_path (BoltProxy *proxy)
G_DEPRECATED_FOR (g_dbus_proxy_get_object_path);

gboolean          bolt_proxy_get_bool_by_pspec (gpointer    proxy,
                                                GParamSpec *spec);

gint              bolt_proxy_get_enum_by_pspec (gpointer    proxy,
                                                GParamSpec *spec);

guint             bolt_proxy_get_flags_by_pspec (gpointer    proxy,
                                                 GParamSpec *spec);

guint32           bolt_proxy_get_uint32_by_pspec (gpointer    proxy,
                                                  GParamSpec *spec);

gint64            bolt_proxy_get_int64_by_pspec (gpointer    proxy,
                                                 GParamSpec *spec);

guint64           bolt_proxy_get_uint64_by_pspec (gpointer    proxy,
                                                  GParamSpec *spec);

const char *      bolt_proxy_get_string_by_pspec (gpointer    proxy,
                                                  GParamSpec *spec);

char **           bolt_proxy_get_strv_by_pspec (gpointer    proxy,
                                                GParamSpec *spec);

gboolean          bolt_proxy_set_property (BoltProxy    *proxy,
                                           const char   *name,
                                           GVariant     *value,
                                           GCancellable *cancellable,
                                           GError      **error);

void              bolt_proxy_set_property_async (BoltProxy          *proxy,
                                                 const char         *name,
                                                 GVariant           *value,
                                                 GCancellable       *cancellable,
                                                 GAsyncReadyCallback callback,
                                                 gpointer            user_data);

gboolean         bolt_proxy_set_property_finish (GAsyncResult *res,
                                                 GError      **error);

gboolean         bolt_proxy_set (BoltProxy    *proxy,
                                 GParamSpec   *spec,
                                 const GValue *value,
                                 GCancellable *cancellable,
                                 GError      **error);

void             bolt_proxy_set_async (BoltProxy          *proxy,
                                       GParamSpec         *spec,
                                       const GValue       *value,
                                       GCancellable       *cancellable,
                                       GAsyncReadyCallback callback,
                                       gpointer            user_data);

gboolean         bolt_proxy_set_finish (GAsyncResult *res,
                                        GError      **error);

void              bolt_proxy_property_setter (GObject      *object,
                                              guint         prop_id,
                                              const GValue *value,
                                              GParamSpec   *spec);

G_END_DECLS

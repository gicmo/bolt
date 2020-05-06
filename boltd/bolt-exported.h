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

#pragma once

#include <bolt-glue.h>

#include <gio/gio.h>

G_BEGIN_DECLS

#define BOLT_TYPE_EXPORTED bolt_exported_get_type ()

G_DECLARE_DERIVABLE_TYPE (BoltExported, bolt_exported, BOLT, EXPORTED, GObject);

typedef struct _BoltExportedClassPrivate BoltExportedClassPrivate;

struct _BoltExportedClass
{
  GObjectClass parent_class;

  /*< private >*/
  BoltExportedClassPrivate *priv;

  /*< public >*/

  /* Signals */
  gboolean (*authorize_method) (BoltExported          *exported,
                                GDBusMethodInvocation *invocation,
                                GError               **error);

  gboolean (*authorize_property) (BoltExported          *exported,
                                  const char            *name,
                                  gboolean               setting,
                                  GDBusMethodInvocation *invocation,
                                  GError               **error);

  /* for the future */
  gpointer padding[10];
};

typedef GVariant *  (* BoltExportedMethodHandler) (BoltExported          *obj,
                                                   GVariant              *params,
                                                   GDBusMethodInvocation *inv,
                                                   GError               **error);

typedef gboolean (* BoltExportedSetter) (BoltExported *obj,
                                         const char   *name,
                                         const GValue *value,
                                         GError      **error);

/* class methods  */
void     bolt_exported_class_set_interface_name (BoltExportedClass *klass,
                                                 const char        *name);

void     bolt_exported_class_set_interface_info (BoltExportedClass *klass,
                                                 const char        *iface_name,
                                                 const char        *resource_name);

void     bolt_exported_class_set_object_path (BoltExportedClass *klass,
                                              const char        *base_path);

void     bolt_exported_class_export_property (BoltExportedClass *klass,
                                              GParamSpec        *spec);

void     bolt_exported_class_export_properties (BoltExportedClass *klass,
                                                guint              start,
                                                guint              n_pspecs,
                                                GParamSpec       **specs);

void     bolt_exported_class_property_setter (BoltExportedClass *klass,
                                              GParamSpec        *spec,
                                              BoltExportedSetter setter);

void     bolt_exported_class_property_wireconv (BoltExportedClass *klass,
                                                GParamSpec        *spec,
                                                const char        *custom_id,
                                                BoltConvToWire     to_wire,
                                                BoltConvFromWire   from_wire);

void     bolt_exported_class_export_method (BoltExportedClass        *klass,
                                            const char               *name,
                                            BoltExportedMethodHandler handler);

/* instance methods */
gboolean           bolt_exported_export (BoltExported    *exported,
                                         GDBusConnection *connection,
                                         const char      *object_path,
                                         GError         **error);

gboolean           bolt_exported_unexport (BoltExported *exported);

gboolean           bolt_exported_is_exported (BoltExported *exported);

GDBusConnection *  bolt_exported_get_connection (BoltExported *exported);

const char *       bolt_exported_get_object_path (BoltExported *exported);

gboolean           bolt_exported_emit_signal (BoltExported *exported,
                                              const char   *name,
                                              GVariant     *parameters,
                                              GError      **error);

void               bolt_exported_flush (BoltExported *exported);

G_END_DECLS

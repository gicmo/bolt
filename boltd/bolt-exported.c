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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Christian J. Kellner <christian@kellner.me>
 */

#include "config.h"

#include "bolt-enums.h"
#include "bolt-error.h"
#include "bolt-log.h"
#include "bolt-str.h"

#include "bolt-exported.h"

/* Each element must only contain the ASCII characters "[A-Z][a-z][0-9]_" */
#define DBUS_OPATH_VALID_CHARS "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_"

typedef struct _BoltExportedMethod BoltExportedMethod;
typedef struct _BoltExportedProp   BoltExportedProp;

static GVariant * bolt_exported_get_prop (BoltExported     *exported,
                                          BoltExportedProp *prop);

static char *     bolt_exported_make_object_path (BoltExported *exported);

static void       bolt_exported_dispatch_properties_changed (GObject     *object,
                                                             guint        n_pspecs,
                                                             GParamSpec **pspecs);

static gboolean   handle_authorize_method_default (BoltExported          *exported,
                                                   GDBusMethodInvocation *inv,
                                                   GError               **error);

static gboolean   handle_authorize_property_default (BoltExported          *exported,
                                                     const char            *name,
                                                     gboolean               setting,
                                                     GDBusMethodInvocation *invocation,
                                                     GError               **error);

static void       bolt_exported_method_free (gpointer data);

static void       bolt_exported_prop_free (gpointer data);

static GVariant * bolt_exported_prop_gvalue_to_gvariant (BoltExportedProp *prop,
                                                         const GValue     *value);

static gboolean   bolt_exported_prop_gvariant_to_gvalue (BoltExportedProp *prop,
                                                         GVariant         *variant,
                                                         GValue           *value,
                                                         GError          **error);

struct _BoltExportedMethod
{
  char                     *name;
  BoltExportedMethodHandler handler;
};

struct _BoltExportedProp
{
  GParamSpec   *spec;
  const char   *name_obj; /* shortcut for spec->name */
  const char   *name_bus;

  GVariantType *signature;

  /* optional */
  BoltExportedSetter setter;

  /* auto string conversion */
  GEnumClass  *enum_class; /* shortcut for spec->enum_class */
  GFlagsClass *flags_class; /* shortcut for spec->enum_class */
  gboolean     object_conv;
};

struct _BoltExportedClassPrivate
{
  GDBusNodeInfo      *node_info;
  char               *iface_name;
  GDBusInterfaceInfo *iface_info;
  char               *object_path;

  GHashTable         *methods;
  GHashTable         *properties;

};

typedef struct _BoltExportedPrivate
{
  GDBusConnection *dbus;
  char            *object_path;

  /* if exported */
  guint registration;

  /* property changes */
  GPtrArray *props_changed;
  guint      props_changed_id;

} BoltExportedPrivate;

static gpointer bolt_exported_parent_class = NULL;
static gint BoltExported_private_offset = 0;

static void     bolt_exported_init (GTypeInstance *,
                                    gpointer g_class);
static void     bolt_exported_class_init (BoltExportedClass *klass);
static void     bolt_exported_base_init (gpointer g_class);
static void     bolt_exported_base_finalize (gpointer g_class);

#define GET_PRIV(self) G_STRUCT_MEMBER_P (self, BoltExported_private_offset)
#define CHAIN_UP(method) G_OBJECT_CLASS (bolt_exported_parent_class)->method

GType
bolt_exported_get_type (void)
{
  static volatile gsize exported_type = 0;

  if (g_once_init_enter (&exported_type))
    {
      GType type_id;
      const GTypeInfo type_info = {
        sizeof (BoltExportedClass),
        bolt_exported_base_init,
        (GBaseFinalizeFunc) bolt_exported_base_finalize,
        (GClassInitFunc) bolt_exported_class_init,
        NULL,           /* class_finalize */
        NULL,           /* class_data */
        sizeof (BoltExported),
        0,              /* n_preallocs */
        bolt_exported_init,
        NULL,           /* value_table */
      };

      type_id = g_type_register_static (G_TYPE_OBJECT, "BoltExported",
                                        &type_info, G_TYPE_FLAG_ABSTRACT);

      g_type_add_class_private (type_id, sizeof (BoltExportedClassPrivate));

      BoltExported_private_offset =
        g_type_add_instance_private (type_id, sizeof (BoltExportedPrivate));

      g_once_init_leave (&exported_type, type_id);
    }

  return exported_type;
}

enum {
  PROP_0,

  PROP_OBJECT_ID,
  PROP_OBJECT_PATH,
  PROP_EXPORTED,

  PROP_LAST
};

static GParamSpec *props[PROP_LAST] = {NULL, };

enum {
  SIGNAL_AUTHORIZE_METHOD,
  SIGNAL_AUTHORIZE_PROPERTY,

  SIGNAL_LAST
};


static guint signals[SIGNAL_LAST] = {0, };

static void
bolt_exported_finalize (GObject *object)
{
  BoltExported *exported = BOLT_EXPORTED (object);
  BoltExportedPrivate *priv = GET_PRIV (exported);

  if (bolt_exported_is_exported (exported))
    bolt_exported_unexport (exported);

  g_clear_pointer (&priv->object_path, g_free);
  g_ptr_array_free (priv->props_changed, TRUE);

  G_OBJECT_CLASS (bolt_exported_parent_class)->finalize (object);
}


static void
bolt_exported_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  BoltExported *exported = BOLT_EXPORTED (object);
  BoltExportedPrivate *priv = GET_PRIV (exported);

  switch (prop_id)
    {
    case PROP_OBJECT_ID:
      bolt_bug ("BoltExported::object-id must be overridden");
      break;

    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;

    case PROP_EXPORTED:
      g_value_set_boolean (value, priv->registration > 0);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bolt_exported_init (GTypeInstance *instance, gpointer g_class)
{
  BoltExported *exported = BOLT_EXPORTED (instance);
  BoltExportedPrivate *priv = GET_PRIV (exported);

  priv->props_changed = g_ptr_array_new ();
}

static void
bolt_exported_class_init (BoltExportedClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  bolt_exported_parent_class = g_type_class_peek_parent (klass);
  g_type_class_adjust_private_offset (klass, &BoltExported_private_offset);

  gobject_class->finalize = bolt_exported_finalize;
  gobject_class->get_property = bolt_exported_get_property;
  gobject_class->dispatch_properties_changed = bolt_exported_dispatch_properties_changed;

  klass->authorize_method = handle_authorize_method_default;
  klass->authorize_property = handle_authorize_property_default;

  props[PROP_OBJECT_ID] =
    g_param_spec_string ("object-id",
                         NULL, NULL,
                         NULL,
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_NICK);

  props[PROP_OBJECT_PATH] =
    g_param_spec_string ("object-path",
                         NULL, NULL,
                         NULL,
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_NICK);

  props[PROP_EXPORTED] =
    g_param_spec_boolean ("exported", NULL, NULL,
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_NICK);

  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     props);

  signals[SIGNAL_AUTHORIZE_METHOD] =
    g_signal_new ("authorize-method",
                  BOLT_TYPE_EXPORTED,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (BoltExportedClass, authorize_method),
                  g_signal_accumulator_first_wins,
                  NULL,
                  NULL,
                  G_TYPE_BOOLEAN,
                  2,
                  G_TYPE_DBUS_METHOD_INVOCATION,
                  G_TYPE_POINTER);

  signals[SIGNAL_AUTHORIZE_PROPERTY] =
    g_signal_new ("authorize-property",
                  BOLT_TYPE_EXPORTED,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (BoltExportedClass, authorize_property),
                  g_signal_accumulator_first_wins,
                  NULL,
                  NULL,
                  G_TYPE_BOOLEAN,
                  4,
                  G_TYPE_STRING,
                  G_TYPE_BOOLEAN,
                  G_TYPE_DBUS_METHOD_INVOCATION,
                  G_TYPE_POINTER);

}

static void
bolt_exported_base_init (gpointer g_class)
{
  BoltExportedClass *klass = g_class;

  klass->priv = G_TYPE_CLASS_GET_PRIVATE (g_class, BOLT_TYPE_EXPORTED, BoltExportedClassPrivate);
  memset (klass->priv, 0, sizeof (BoltExportedClassPrivate));

  klass->priv->methods = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                NULL, bolt_exported_method_free);

  klass->priv->properties = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                   NULL, bolt_exported_prop_free);
}

static void
bolt_exported_base_finalize (gpointer g_class)
{
  BoltExportedClass *klass = g_class;
  BoltExportedClassPrivate *priv = klass->priv;

  if (priv->node_info)
    {
      g_dbus_node_info_unref (priv->node_info);
      priv->node_info = NULL;
    }

  g_hash_table_unref (priv->properties);
  g_hash_table_unref (priv->methods);

  g_clear_pointer (&priv->iface_name, g_free);
  g_clear_pointer (&priv->object_path, g_free);
}

/* internal utility functions  */

static const char *
bolt_exported_get_iface_name (BoltExported *exported)
{
  BoltExportedClass *klass;

  klass = BOLT_EXPORTED_GET_CLASS (exported);

  return klass->priv->iface_name;
}

static BoltExportedProp *
bolt_exported_lookup_property (BoltExported *exported,
                               const char   *name,
                               GError      **error)
{
  BoltExportedClass *klass;
  BoltExportedProp *prop;

  if (name == NULL)
    return NULL;

  klass = BOLT_EXPORTED_GET_CLASS (exported);

  prop = g_hash_table_lookup (klass->priv->properties, name);

  if (prop == NULL)
    g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
                 "no such property: %s", name);

  return prop;
}

static BoltExportedMethod *
bolt_exported_lookup_method (BoltExported *exported,
                             const char   *name,
                             GError      **error)
{
  BoltExportedClass *klass;
  BoltExportedMethod *method;

  if (name == NULL)
    return NULL;

  klass = BOLT_EXPORTED_GET_CLASS (exported);

  method = g_hash_table_lookup (klass->priv->methods, name);

  if (method == NULL)
    g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
                 "no such method: %s", name);

  return method;
}

static GVariant *
bolt_exported_get_prop (BoltExported     *exported,
                        BoltExportedProp *prop)
{
  g_auto(GValue) res = G_VALUE_INIT;
  const char *name;
  const GParamSpec *spec;
  GVariant *ret;

  name = prop->name_obj;
  spec = prop->spec;

  g_value_init (&res, spec->value_type);
  g_object_get_property (G_OBJECT (exported), name, &res);

  ret = bolt_exported_prop_gvalue_to_gvariant (prop, &res);

  return ret;
}

static char *
bolt_exported_make_object_path (BoltExported *exported)
{
  g_autofree char *id = NULL;
  BoltExportedClass *klass;
  const char *base;

  klass = BOLT_EXPORTED_GET_CLASS (exported);
  base = klass->priv->object_path;

  g_object_get (exported, "object-id", &id, NULL);

  if (id)
    g_strcanon (id, DBUS_OPATH_VALID_CHARS, '_');

  if (base && id)
    return g_build_path ("/", "/", base, id, NULL);
  else if (base)
    return g_build_path ("/", "/", base, NULL);
  else if (id)
    return g_build_path ("/", "/", id, NULL);

  return g_strdup ("/");
}

/* dispatch helper function */

typedef struct _DispatchData
{

  GDBusMethodInvocation *inv;

  gboolean               is_property;

  union
  {
    BoltExportedMethod *method;
    BoltExportedProp   *prop;
  };

} DispatchData;

static void
dispatch_data_free (DispatchData *data)
{
  g_slice_free (DispatchData, data);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (DispatchData, dispatch_data_free);

static GVariant *
dispach_property_setter (BoltExported          *exported,
                         GDBusMethodInvocation *inv,
                         BoltExportedProp      *prop,
                         GError               **error)
{
  g_autoptr(GError) err = NULL;
  g_auto(GValue) val = G_VALUE_INIT;
  g_autoptr(GVariant) vin = NULL;
  GVariant *params;
  gboolean ok;

  params = g_dbus_method_invocation_get_parameters (inv);

  g_variant_get_child (params, 2, "v", &vin);
  g_value_init (&val, prop->spec->value_type);

  ok = bolt_exported_prop_gvariant_to_gvalue (prop, vin, &val, &err);

  if (ok)
    ok = prop->setter (exported, prop->name_obj, &val, &err);

  if (!ok && err != NULL)
    {
      g_propagate_error (error, g_steal_pointer (&err));
      return NULL;
    }
  else if (!ok)
    {
      bolt_critical (LOG_TOPIC ("dbus"),
                     "property setter signaled error, but no error is set");
      g_set_error (error, BOLT_ERROR, BOLT_ERROR_FAILED,
                   "%s", "could not set property");
      return NULL;
    }

  g_object_notify_by_pspec (G_OBJECT (exported), prop->spec);
  return g_variant_new ("()");
}

static GVariant *
dispatch_method_call (BoltExported          *exported,
                      GDBusMethodInvocation *inv,
                      BoltExportedMethod    *method,
                      GError               **error)
{
  GVariant *params = g_dbus_method_invocation_get_parameters (inv);
  GVariant *res;

  res = method->handler (exported, params, inv, error);

  return res;
}

static void
query_authorization_done (GObject      *source_object,
                          GAsyncResult *res,
                          gpointer      user_data)
{
  g_autoptr(GError) err = NULL;
  g_autoptr(DispatchData) data = user_data;
  GDBusMethodInvocation *inv = data->inv;
  BoltExported *exported = BOLT_EXPORTED (source_object);
  GVariant *ret;
  gboolean ok;

  ok = g_task_propagate_boolean (G_TASK (res), &err);

  bolt_debug (LOG_TOPIC ("dbus"), "authorization done: %s", bolt_yesno (ok));

  if (!ok && err == NULL)
    {
      bolt_bug ("negative auth result, but no GError set");
      g_set_error_literal (&err, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED,
                           "access denied");
    }

  if (!ok)
    {
      g_dbus_method_invocation_return_gerror (inv, err);
      return;
    }

  if (data->is_property)
    ret = dispach_property_setter (exported, inv, data->prop, &err);
  else
    ret = dispatch_method_call (exported, inv, data->method, &err);

  if (ret == NULL && err != NULL)
    g_dbus_method_invocation_return_gerror (inv, err);
  else if (ret != NULL)
    g_dbus_method_invocation_return_value (inv, ret);
  /* else: must have been handled by the method call directly */
}

static void
query_authorization (GTask        *task,
                     gpointer      source_object,
                     gpointer      task_data,
                     GCancellable *cancellable)
{
  GError *error = NULL;
  BoltExported *exported = source_object;
  DispatchData *data = task_data;
  gboolean authorized = FALSE;

  if (data->is_property)
    {
      const char *method_name = g_dbus_method_invocation_get_method_name (data->inv);
      gboolean is_setter = bolt_streq (method_name, "Set");

      g_signal_emit (exported,
                     signals[SIGNAL_AUTHORIZE_PROPERTY],
                     0,
                     data->prop->name_obj,
                     is_setter,
                     data->inv,
                     &error,
                     &authorized);
    }
  else
    {
      g_signal_emit (exported,
                     signals[SIGNAL_AUTHORIZE_METHOD],
                     0,
                     data->inv,
                     &error,
                     &authorized);
    }

  bolt_debug (LOG_TOPIC ("dbus"), "query_authorization returned: %s",
              bolt_yesno (authorized));

  if (!authorized)
    g_task_return_error (task, error);
  else
    g_task_return_boolean (task, authorized);
}

static gboolean
handle_authorize_method_default (BoltExported          *exported,
                                 GDBusMethodInvocation *inv,
                                 GError               **error)
{
  const char *method_name;

  method_name = g_dbus_method_invocation_get_method_name (inv);
  g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED,
               "bolt operation '%s' denied by default policy",
               method_name);

  return FALSE;
}

static gboolean
handle_authorize_property_default (BoltExported          *exported,
                                   const char            *name,
                                   gboolean               setting,
                                   GDBusMethodInvocation *inv,
                                   GError               **error)
{
  g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED,
               "setting property '%s' denied by default policy",
               name);

  return FALSE;
}

/* DBus virtual table */

static void
handle_dbus_method_call (GDBusConnection       *connection,
                         const char            *sender,
                         const char            *object_path,
                         const char            *interface_name,
                         const char            *method_name,
                         GVariant              *parameters,
                         GDBusMethodInvocation *invocation,
                         gpointer               user_data)
{
  g_autoptr(GTask) task = NULL;
  g_autoptr(GError) err = NULL;
  BoltExported *exported;
  gboolean is_property;
  DispatchData *data;

  exported = BOLT_EXPORTED (user_data);

  bolt_debug (LOG_TOPIC ("dbus"), "method call: %s.%s at %s from %s",
              interface_name, method_name, object_path, sender);

  /* we also handle property setting here */
  is_property = bolt_streq (interface_name, "org.freedesktop.DBus.Properties");

  data = g_slice_new0 (DispatchData);
  data->inv = invocation;
  data->is_property = is_property;

  if (is_property)
    {
      const GDBusPropertyInfo *pi;
      BoltExportedProp *prop = NULL;
      gboolean is_setter;

      pi = g_dbus_method_invocation_get_property_info (invocation);

      if (pi != NULL)
        prop = bolt_exported_lookup_property (exported, pi->name, &err);
      else
        g_set_error (&err, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                     "property information missing");

      is_setter = bolt_streq (method_name, "Set");

      if (is_setter && prop != NULL && prop->setter == NULL)
        g_set_error (&err, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                     "property: %s has no setter", prop->name_bus);

      data->prop = prop;
    }
  else
    {
      BoltExportedMethod *method;

      method = bolt_exported_lookup_method (exported, method_name, &err);
      data->method = method;
    }

  if (err != NULL)
    {
      //bolt_warn_err (err, LOG_TOPIC ("dbus"), "error dispatching call");
      g_dbus_method_invocation_return_gerror (invocation, err);
      return;
    }

  task = g_task_new (exported, NULL, query_authorization_done, data);

  g_task_set_source_tag (task, handle_dbus_method_call);
  g_task_set_task_data (task, data, NULL);

  g_task_run_in_thread (task, query_authorization);
}

static GVariant *
handle_dbus_get_property (GDBusConnection *connection,
                          const char      *sender,
                          const char      *object_path,
                          const char      *interface_name,
                          const char      *property_name,
                          GError         **error,
                          gpointer         user_data)
{
  g_autoptr(GError) err = NULL;
  BoltExported *exported;
  BoltExportedProp *prop;
  GVariant *ret;

  exported = BOLT_EXPORTED (user_data);

  bolt_debug (LOG_TOPIC ("dbus"), "get property: %s.%s at %s from %s",
              interface_name, property_name, object_path, sender);

  prop = bolt_exported_lookup_property (exported, property_name, &err);
  if (prop == NULL)
    {
      bolt_warn_err (err, LOG_TOPIC ("dbus"), "get_property");
      g_propagate_error (error, g_steal_pointer (&err));
      return NULL;
    }

  ret = bolt_exported_get_prop (exported, prop);

  return ret;
}

static void
bolt_exported_dispatch_properties_changed (GObject     *object,
                                           guint        n_pspecs,
                                           GParamSpec **pspecs)
{
  g_autoptr(GVariant) changes = NULL;
  g_autoptr(GError) err = NULL;
  g_auto(GVariantBuilder) changed;
  g_auto(GVariantBuilder) invalidated;
  const char *iface_name;
  BoltExported *exported;
  BoltExportedPrivate *priv;
  gboolean ok;
  guint count = 0;

  exported = BOLT_EXPORTED (object);
  priv = GET_PRIV (exported);

  g_variant_builder_init (&changed, G_VARIANT_TYPE ("a{sv}"));
  g_variant_builder_init (&invalidated, G_VARIANT_TYPE ("as"));

  /* no bus, no changed signal */
  if (priv->dbus == NULL || priv->object_path == NULL)
    goto out;

  for (guint i = 0; i < n_pspecs; i++)
    {
      g_autoptr(GVariant) var = NULL;
      GParamSpec *pspec = pspecs[i];
      BoltExportedProp *prop;
      const char *nick;

      nick = g_param_spec_get_nick (pspec);
      prop =  bolt_exported_lookup_property (exported, nick, NULL);

      if (prop == NULL)
        {
          bolt_debug (LOG_TOPIC ("dbus"), "prop %s change ignored", nick);
          continue;
        }

      bolt_debug (LOG_TOPIC ("dbus"), "prop %s changed", nick);

      var = bolt_exported_get_prop (exported, prop);
      g_variant_builder_add (&changed, "{sv}", prop->name_bus, var);
      count++;
    }

  if (count == 0)
    goto out;

  iface_name = bolt_exported_get_iface_name (exported);
  changes = g_variant_ref_sink (g_variant_new ("(sa{sv}as)",
                                               iface_name,
                                               &changed,
                                               &invalidated));

  ok = g_dbus_connection_emit_signal (priv->dbus,
                                      NULL,
                                      priv->object_path,
                                      "org.freedesktop.DBus.Properties",
                                      "PropertiesChanged",
                                      changes,
                                      &err);

  if (!ok)
    bolt_warn_err (err, LOG_TOPIC ("dbus"),
                   "error emitting property changes");

  bolt_debug (LOG_TOPIC ("dbus"), "emitted property %u changes", count);

out:
  CHAIN_UP (dispatch_properties_changed) (object, n_pspecs, pspecs);
}

static GDBusInterfaceVTable dbus_vtable = {
  handle_dbus_method_call,
  handle_dbus_get_property,
  NULL, /* set_property (handled by method call) */
};

/* public methods: class */

void
bolt_exported_class_set_interface_name (BoltExportedClass *klass,
                                        const char        *name)
{
  g_return_if_fail (BOLT_IS_EXPORTED_CLASS (klass));
  g_return_if_fail (klass->priv != NULL);
  g_return_if_fail (klass->priv->iface_name == NULL);

  klass->priv->iface_name = g_strdup (name);
}

void
bolt_exported_class_set_interface_info_from_xml (BoltExportedClass *klass,
                                                 const char        *xml)
{
  g_autoptr(GError) error = NULL;
  BoltExportedClassPrivate *priv;
  GDBusInterfaceInfo **iter;
  GDBusInterfaceInfo *info = NULL;

  g_return_if_fail (BOLT_IS_EXPORTED_CLASS (klass));
  g_return_if_fail (klass->priv != NULL);
  g_return_if_fail (klass->priv->node_info == NULL);

  priv = klass->priv;

  priv->node_info = g_dbus_node_info_new_for_xml (xml, &error);
  if (priv->node_info == NULL)
    bolt_error (LOG_TOPIC ("dbus"), LOG_ERR (error),
                "failed to load xml");

  for (iter = klass->priv->node_info->interfaces; iter && *iter; iter++)
    {
      GDBusInterfaceInfo *ii = *iter;
      if (bolt_streq (ii->name, klass->priv->iface_name))
        {
          info = ii;
          break;
        }
    }

  if (info == NULL)
    bolt_error (LOG_TOPIC ("dbus"),
                "interface information is missing");

  priv->iface_info = info;
}

void
bolt_exported_class_set_interface_info (BoltExportedClass *klass,
                                        const char        *iface_name,
                                        const char        *resource_name)
{
  g_autoptr(GBytes) data = NULL;
  g_autoptr(GError) err = NULL;
  const char *xml;

  bolt_exported_class_set_interface_name (klass, iface_name);

  data = g_resources_lookup_data (resource_name,
                                  G_RESOURCE_LOOKUP_FLAGS_NONE,
                                  &err);

  if (data == NULL)
    {
      bolt_error (LOG_TOPIC ("dbus"), LOG_ERR (err),
                  "could not load resource");
      return;
    }

  xml = g_bytes_get_data (data, NULL);
  bolt_exported_class_set_interface_info_from_xml (klass, xml);
}

void
bolt_exported_class_set_object_path (BoltExportedClass *klass,
                                     const char        *base_path)
{
  g_return_if_fail (BOLT_IS_EXPORTED_CLASS (klass));
  g_return_if_fail (klass->priv != NULL);
  g_return_if_fail (klass->priv->object_path == NULL);
  g_return_if_fail (base_path != NULL);
  g_return_if_fail (g_variant_is_object_path (base_path));

  klass->priv->object_path = g_strdup (base_path);
}

void
bolt_exported_class_export_property (BoltExportedClass *klass,
                                     GParamSpec        *spec)
{
  BoltExportedClassPrivate *priv;
  GDBusInterfaceInfo *iface = NULL;
  GDBusPropertyInfo *info = NULL;
  GDBusPropertyInfo **iter;
  BoltExportedProp *prop;
  const char *name_bus, *name_obj;
  gboolean is_str_prop;
  const char *conv = NULL;

  if (!klass || !BOLT_IS_EXPORTED_CLASS (klass))
    {
      bolt_error (LOG_TOPIC ("dbus"), "klass not a BoltExportedClass");
      return;
    }

  priv = klass->priv;

  name_obj = g_param_spec_get_name (spec);
  name_bus = g_param_spec_get_nick (spec);

  iface = priv->iface_info;

  for (iter = iface->properties; iter && *iter; iter++)
    {
      GDBusPropertyInfo *pi = *iter;
      if (bolt_streq (pi->name, name_bus))
        {
          info = pi;
          break;
        }
    }

  if (info == NULL)
    {
      bolt_error (LOG_TOPIC ("dbus"), "no property info for %s", name_bus);
      return;
    }

  prop = g_new0 (BoltExportedProp, 1);

  prop->spec = g_param_spec_ref (spec);
  prop->name_bus = name_bus;
  prop->name_obj = name_obj;

  prop->signature = g_variant_type_new (info->signature);

  is_str_prop = g_variant_type_equal (prop->signature, G_VARIANT_TYPE_STRING);

  if (is_str_prop && G_IS_PARAM_SPEC_ENUM (prop->spec))
    {
      GParamSpecEnum *enum_spec = G_PARAM_SPEC_ENUM (prop->spec);
      prop->enum_class = enum_spec->enum_class;
      conv = " [enum-auto-convert]";
    }
  else if (is_str_prop && G_IS_PARAM_SPEC_FLAGS (prop->spec))
    {
      GParamSpecFlags *flags_spec = G_PARAM_SPEC_FLAGS (prop->spec);
      prop->flags_class = flags_spec->flags_class;
      conv = " [flags-auto-convert]";
    }
  else if (is_str_prop && G_IS_PARAM_SPEC_OBJECT (prop->spec))
    {
      prop->object_conv = TRUE;
      conv = " [object-auto-convert]";
    }

  bolt_debug (LOG_TOPIC ("dbus"), "installed prop: %s -> %s%s",
              prop->name_bus, prop->name_obj,
              conv ? : "");

  g_hash_table_insert (priv->properties, (gpointer) prop->name_bus, prop);
}

void
bolt_exported_class_export_properties (BoltExportedClass *klass,
                                       guint              start,
                                       guint              n_pspecs,
                                       GParamSpec       **specs)
{
  g_return_if_fail (start > 0);

  for (guint i = start; i < n_pspecs; i++)
    bolt_exported_class_export_property (klass, specs[i]);
}

void
bolt_exported_class_property_setter (BoltExportedClass *klass,
                                     GParamSpec        *spec,
                                     BoltExportedSetter setter)
{
  BoltExportedProp *prop;
  const char *nick;

  if (!klass || !BOLT_IS_EXPORTED_CLASS (klass))
    {
      bolt_error (LOG_TOPIC ("dbus"), "klass not a BoltExportedClass");
      return;
    }

  nick = g_param_spec_get_nick (spec);
  prop = g_hash_table_lookup (klass->priv->properties, nick);

  if (prop == NULL)
    {
      bolt_error (LOG_TOPIC ("dbus"), "unknown property: %s", nick);
      return;
    }

  prop->setter = setter;
}

void
bolt_exported_class_export_method (BoltExportedClass        *klass,
                                   const char               *name,
                                   BoltExportedMethodHandler handler)
{
  BoltExportedMethod *method;

  method = g_new0 (BoltExportedMethod, 1);

  method->name = g_strdup (name);
  method->handler = handler;

  g_hash_table_insert (klass->priv->methods, method->name, method);
}


/* public methods: instance */
gboolean
bolt_exported_export (BoltExported    *exported,
                      GDBusConnection *connection,
                      const char      *object_path,
                      GError         **error)
{
  BoltExportedPrivate *priv;
  BoltExportedClass *klass;
  guint id;

  g_return_val_if_fail (BOLT_IS_EXPORTED (exported), FALSE);
  g_return_val_if_fail (connection != NULL, FALSE);

  priv = GET_PRIV (exported);
  klass = BOLT_EXPORTED_GET_CLASS (exported);

  if (klass->priv->iface_info == NULL)
    {
      g_set_error (error, BOLT_ERROR, BOLT_ERROR_FAILED,
                   "interface information is missing");
      return FALSE;
    }

  if (object_path == NULL)
    {
      object_path = bolt_exported_make_object_path (exported);
      bolt_debug (LOG_TOPIC ("dbus"), "generated object path: %s",
                  object_path);
    }

  if (object_path == NULL)
    {
      g_set_error_literal (error, BOLT_ERROR, BOLT_ERROR_FAILED,
                           "object path empty for object");
      return FALSE;
    }

  id = g_dbus_connection_register_object (connection,
                                          object_path,
                                          klass->priv->iface_info,
                                          &dbus_vtable,
                                          exported,
                                          NULL,
                                          error);

  if (id == 0)
    return FALSE;

  bolt_debug (LOG_TOPIC ("dbus"), "registered object at %s", object_path);

  priv->dbus = g_object_ref (connection);
  priv->object_path = g_strdup (object_path);
  priv->registration = id;

  g_object_notify_by_pspec (G_OBJECT (exported), props[PROP_OBJECT_PATH]);
  g_object_notify_by_pspec (G_OBJECT (exported), props[PROP_EXPORTED]);

  return TRUE;
}

gboolean
bolt_exported_unexport (BoltExported *exported)
{
  g_autofree char *opath = NULL;
  BoltExportedPrivate *priv;
  gboolean ok;

  g_return_val_if_fail (BOLT_IS_EXPORTED (exported), FALSE);

  priv = GET_PRIV (exported);

  if (priv->dbus == NULL || priv->registration == 0)
    return FALSE;

  ok = g_dbus_connection_unregister_object (priv->dbus, priv->registration);

  if (ok)
    {
      g_clear_object (&priv->dbus);
      priv->registration = 0;
      opath = g_steal_pointer (&priv->object_path);
      g_object_notify_by_pspec (G_OBJECT (exported), props[PROP_OBJECT_PATH]);
      g_object_notify_by_pspec (G_OBJECT (exported), props[PROP_EXPORTED]);
    }

  bolt_debug (LOG_TOPIC ("dbus"), "unregistered object at %s: %s",
              opath, bolt_yesno (ok));

  return ok;
}

gboolean
bolt_exported_is_exported (BoltExported *exported)
{
  BoltExportedPrivate *priv;

  g_return_val_if_fail (BOLT_IS_EXPORTED (exported), FALSE);

  priv = GET_PRIV (exported);

  return priv->registration > 0;
}

GDBusConnection *
bolt_exported_get_connection (BoltExported *exported)
{
  BoltExportedPrivate *priv;

  g_return_val_if_fail (BOLT_IS_EXPORTED (exported), NULL);

  priv = GET_PRIV (exported);
  return priv->dbus;
}

const char *
bolt_exported_get_object_path (BoltExported *exported)
{
  BoltExportedPrivate *priv;

  g_return_val_if_fail (BOLT_IS_EXPORTED (exported), NULL);

  priv = GET_PRIV (exported);

  return priv->object_path;
}

gboolean
bolt_exported_emit_signal (BoltExported *exported,
                           const char   *name,
                           GVariant     *parameters,
                           GError      **error)
{
  g_autoptr(GError) err = NULL;
  BoltExportedPrivate *priv;
  const char *iface_name;
  gboolean ok;

  g_return_val_if_fail (BOLT_IS_EXPORTED (exported), FALSE);

  priv = GET_PRIV (exported);

  /* if we are not exported, we just ignore this */
  if (priv->dbus == NULL || priv->object_path == NULL)
    return TRUE;

  iface_name = bolt_exported_get_iface_name (exported);

  ok = g_dbus_connection_emit_signal (priv->dbus,
                                      NULL,
                                      priv->object_path,
                                      iface_name,
                                      name,
                                      parameters,
                                      &err);

  if (!ok)
    {
      bolt_warn_err (err, LOG_TOPIC ("dbus"),
                     "error emitting signal");
      g_propagate_error (error, g_steal_pointer (&err));
    }
  else
    {
      bolt_debug (LOG_TOPIC ("dbus"), "emitted signal: %s", name);
    }

  return ok;
}

/* non BoltExported internal methods */

static void
bolt_exported_method_free (gpointer data)
{
  BoltExportedMethod *method = data;

  g_clear_pointer (&method->name, g_free);
  g_free (method);
}

static void
bolt_exported_prop_free (gpointer data)
{
  BoltExportedProp *prop = data;

  g_param_spec_unref (prop->spec);
  g_variant_type_free (prop->signature);



  g_free (prop);
}

static GVariant *
enum_gvalue_to_gvariant (GEnumClass   *enum_class,
                         const GValue *value)
{
  g_autofree char *str = NULL;
  const char *name;
  GEnumValue *ev;
  GVariant *res;
  gint iv;

  iv = g_value_get_enum (value);
  ev = g_enum_get_value (enum_class, iv);

  if (ev != NULL)
    {
      res = g_variant_new_string (ev->value_nick);
      return g_variant_ref_sink (res);
    }

  /* we got an invalid value for that enum */
  str = g_strdup_printf ("%d", iv);
  res = g_variant_new_string (str);
  name = g_type_name_from_class ((GTypeClass *) enum_class);
  bolt_bug ("invalid enum value %d for enum '%s'", iv, name);

  return g_variant_ref_sink (res);
}

static gboolean
enum_gvariant_to_gvalue (GEnumClass *enum_class,
                         GVariant   *variant,
                         GValue     *value,
                         GError    **error)
{
  GEnumValue *ev = NULL;
  const char *str;

  str = g_variant_get_string (variant, NULL);

  if (str == NULL)
    str = "invalid";

  ev = g_enum_get_value_by_nick (enum_class, str);

  if (ev == NULL)
    {
      const char *name;

      name = g_type_name_from_class ((GTypeClass *) enum_class);
      g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                   "invalid enum value '%s' for '%s'", str, name);

      return FALSE;
    }

  g_value_set_enum (value, ev->value);
  return TRUE;
}

static GVariant *
flags_gvalue_to_gvariant (GFlagsClass  *flags_class,
                          const GValue *value)
{
  g_autoptr(GError) err = NULL;
  g_autofree char *str = NULL;
  GVariant *res;
  guint iv;

  iv = g_value_get_flags (value);

  str = bolt_flags_class_to_string (flags_class, iv, &err);
  if (str == NULL)
    {
      const char *name;

      name = g_type_name_from_class ((GTypeClass *) flags_class);
      str = g_strdup_printf ("%u", iv);
      bolt_bug ("invalid enum flags '%u' for enum '%s'", iv, name);
    }

  res = g_variant_new_string (str);
  return g_variant_ref_sink (res);
}

static gboolean
flags_gvariant_to_gvalue (GFlagsClass *flags_class,
                          GVariant    *variant,
                          GValue      *value,
                          GError     **error)
{
  gboolean ok;
  const char *str;
  guint flags;

  str = g_variant_get_string (variant, NULL);

  if (str == NULL)
    {
      const char *name;

      name = g_type_name_from_class ((GTypeClass *) flags_class);
      g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                   "invalid flags value (null) for '%s'", name);
    }

  ok = bolt_flags_class_from_string (flags_class, str, &flags, error);

  if (ok)
    g_value_set_flags (value, flags);

  return ok;
}

static GVariant *
object_gvalue_to_gvariant (const GValue *value)
{
  g_autofree char *str = NULL;
  GVariant *res;
  GObject *obj;

  obj = g_value_get_object (value);

  if (obj)
    g_object_get (obj, "object-id", &str, NULL);

  if (str == NULL)
    str = g_strdup ("");

  res = g_variant_new_string (str);
  return g_variant_ref_sink (res);
}

static gboolean
object_gvariant_to_gvalue (GVariant    *variant G_GNUC_UNUSED,
                           GValue      *value   G_GNUC_UNUSED,
                           GError             **error)
{
  g_set_error_literal (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                       "can not set object property from string");
  return FALSE;
}

static GVariant *
bolt_exported_prop_gvalue_to_gvariant (BoltExportedProp *prop,
                                       const GValue     *value)
{
  if (prop->enum_class != NULL)
    return enum_gvalue_to_gvariant (prop->enum_class, value);
  else if (prop->flags_class != NULL)
    return flags_gvalue_to_gvariant (prop->flags_class, value);
  else if (prop->object_conv)
    return object_gvalue_to_gvariant (value);
  else
    return g_dbus_gvalue_to_gvariant (value, prop->signature);
}

static gboolean
bolt_exported_prop_gvariant_to_gvalue (BoltExportedProp *prop,
                                       GVariant         *variant,
                                       GValue           *value,
                                       GError          **error)
{
  if (prop->enum_class != NULL)
    return enum_gvariant_to_gvalue (prop->enum_class, variant, value, error);
  else if (prop->flags_class != NULL)
    return flags_gvariant_to_gvalue (prop->flags_class, variant, value, error);
  else if (prop->object_conv)
    return object_gvariant_to_gvalue (variant, value, error);

  g_dbus_gvariant_to_gvalue (variant, value);

  return TRUE;
}

GParamSpec *
bolt_param_spec_override (GObjectClass *klass,
                          const char   *name)
{
  GObjectClass *parent_class = NULL;
  GParamSpec *base = NULL;
  GType parent;
  GType type;
  guint n;

  g_return_val_if_fail (G_IS_OBJECT_CLASS (klass), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  type = G_OBJECT_CLASS_TYPE (klass);
  parent = g_type_parent (type);

  parent_class = g_type_class_peek (parent);
  if (parent_class)
    base = g_object_class_find_property (parent_class, name);

  if (base == NULL)
    {
      g_autofree GType *ifaces = g_type_interfaces (type, &n);

      while (n-- && base == NULL)
        {
          gpointer iface = g_type_default_interface_peek (ifaces[n]);

          if (iface)
            base = g_object_interface_find_property (iface, name);
        }
    }

  if (base == NULL)
    {
      bolt_bug ("Could not override unknown property: '%s::%s'",
                G_OBJECT_CLASS_NAME (klass), name);
      return NULL;
    }

  return g_param_spec_override (name, base);
}

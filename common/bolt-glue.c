/*
 * Copyright © 2019 Red Hat, Inc
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

#include "bolt-glue.h"

#include "bolt-enums.h"
#include "bolt-str.h"


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
      g_critical ("Could not override unknown property: '%s::%s'",
                  G_OBJECT_CLASS_NAME (klass), name);
      return NULL;
    }

  return g_param_spec_override (name, base);
}

GPtrArray *
bolt_properties_for_type (GType target)
{
  GObjectClass *klass;
  GParamSpec **specs;
  GPtrArray *props;
  guint n;

  klass = g_type_class_ref (target);
  specs = g_object_class_list_properties (klass, &n);
  props = g_ptr_array_new_full (n, (GDestroyNotify) g_param_spec_unref);

  for (guint i = 0; i < n; i++)
    {
      GParamSpec *s = specs[i];

      if (s->owner_type != target)
        continue;

      g_ptr_array_add (props, g_param_spec_ref (s));
    }

  return props;
}

gboolean
bolt_properties_find (GPtrArray   *specs,
                      const char  *name,
                      GParamSpec **spec,
                      GError     **error)
{

  if (name == NULL || specs == NULL)
    return FALSE;

  for (guint i = 0; i < specs->len; i++)
    {
      GParamSpec *s = g_ptr_array_index (specs, i);

      if (g_str_equal (name, s->name) ||
          bolt_streq (name, g_param_spec_get_nick (s)))
        {
          *spec = s;
          return TRUE;
        }
    }

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
               "property '%s' not found", name);

  return FALSE;
}

typedef enum {
  BOLT_WIRE_CONV_NATIVE,
  BOLT_WIRE_CONV_ENUM_AS_STRING,
  BOLT_WIRE_CONV_FLAGS_AS_STRING,
  BOLT_WIRE_CONV_OBJECT_AS_STRING,
} BoltWireConvType;

typedef GVariant * (*conv_to_wire) (BoltWireConv *conv,
                                    const GValue *value,
                                    GError      **error);

typedef  gboolean (*conv_from_str) (BoltWireConv *conv,
                                    GVariant     *wire,
                                    GValue       *value,
                                    GError      **error);

struct _BoltWireConv
{
  /* book-keeping */
  volatile gint ref_count;

  /*  */
  GVariantType *wire_type;
  GParamSpec   *prop_spec;

  /*  */
  BoltWireConvType conv_type;
  conv_to_wire     to_wire;
  conv_from_str    from_wire;

};

/* internal conversions */
static GVariant *
conv_enum_to_str (BoltWireConv *conv,
                  const GValue *value,
                  GError      **error)
{
  GParamSpecEnum *es = G_PARAM_SPEC_ENUM (conv->prop_spec);
  const char *str = NULL;
  gint iv;

  iv = g_value_get_enum (value);
  str = bolt_enum_class_to_string (es->enum_class, iv, error);

  if (str == NULL)
    return NULL;

  return g_variant_new_string (str);
}

static gboolean
conv_enum_from_str (BoltWireConv *conv,
                    GVariant     *wire,
                    GValue       *value,
                    GError      **error)
{
  GParamSpecEnum *es;
  const char *str;
  gboolean ok;
  gint v;

  es  = G_PARAM_SPEC_ENUM (conv->prop_spec);
  str = g_variant_get_string (wire, NULL);

  /* NB: it is ok to pass NULL for 'str' */
  ok = bolt_enum_class_from_string (es->enum_class, str, &v, error);

  if (ok)
    g_value_set_enum (value, v);
  else
    g_value_set_enum (value, es->default_value);

  return ok;
}

static GVariant *
conv_flags_to_str (BoltWireConv *conv,
                   const GValue *value,
                   GError      **error)
{
  GParamSpecFlags *fs;
  const char *str;
  guint uv;

  fs = G_PARAM_SPEC_FLAGS (conv->prop_spec);
  uv = g_value_get_flags (value);

  str = bolt_flags_class_to_string (fs->flags_class, uv, error);

  if (str == NULL)
    return NULL;

  return g_variant_new_string (str);
}

static gboolean
conv_flags_from_str (BoltWireConv *conv,
                     GVariant     *wire,
                     GValue       *value,
                     GError      **error)
{
  GParamSpecFlags *fs;
  gboolean ok;
  const char *str;
  guint v;

  fs = G_PARAM_SPEC_FLAGS (conv->prop_spec);
  str = g_variant_get_string (wire, NULL);

  /* NB: NULL is a safe value for 'str' to be passed into */
  ok = bolt_flags_class_from_string (fs->flags_class, str, &v, error);

  if (ok)
    g_value_set_flags (value, v);
  else
    g_value_set_flags (value, fs->default_value);

  return ok;
}

static GVariant *
conv_obj_to_str (BoltWireConv *conv,
                 const GValue *value,
                 GError      **error)
{
  GVariant *res = NULL;
  char *str = NULL;
  GObject *obj;

  obj = g_value_get_object (value);

  if (obj)
    g_object_get (obj, "object-id", &str, NULL);
  else
    str = g_strdup ("");

  if (str)
    res = g_variant_new_take_string (str);

  if (!res)
    g_set_error_literal (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                         "failed to convert object to string");

  return res;
}

static gboolean
conv_obj_from_str (BoltWireConv *conv,
                   GVariant     *wire,
                   GValue       *value,
                   GError      **error)
{
  g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
               "failed to convert object from string");
  return FALSE;
}

static GVariant *
conv_value_to_variant (BoltWireConv *conv,
                       const GValue *value,
                       GError      **error)
{
  return g_dbus_gvalue_to_gvariant (value, conv->wire_type);
}

static gboolean
conv_value_from_variant (BoltWireConv *conv,
                         GVariant     *wire,
                         GValue       *value,
                         GError      **error)
{
  g_dbus_gvariant_to_gvalue (wire, value);
  return TRUE;
}

/* public interfaces */
BoltWireConv *
bolt_wire_conv_ref (BoltWireConv *conv)
{
  g_return_val_if_fail (conv != NULL, NULL);
  g_return_val_if_fail (conv->ref_count > 0, NULL);

  g_atomic_int_inc (&conv->ref_count);

  return conv;
}

void
bolt_wire_conv_unref (BoltWireConv *conv)
{
  g_return_if_fail (conv != NULL);
  g_return_if_fail (conv->ref_count > 0);

  if (g_atomic_int_dec_and_test (&conv->ref_count))
    {
      g_variant_type_free (conv->wire_type);
      g_param_spec_unref (conv->prop_spec);
      g_free (conv);
    }
}

const GVariantType *
bolt_wire_conv_get_wire_type (BoltWireConv *conv)
{
  g_return_val_if_fail (conv != NULL, NULL);
  g_return_val_if_fail (conv->ref_count > 0, NULL);

  return conv->wire_type;
}

const GParamSpec *
bolt_wire_conv_get_prop_spec (BoltWireConv *conv)
{
  g_return_val_if_fail (conv != NULL, NULL);
  g_return_val_if_fail (conv->ref_count > 0, NULL);

  return conv->prop_spec;
}

BoltWireConv *
bolt_wire_conv_for (const GVariantType *wire_type,
                    GParamSpec         *prop_spec)
{
  BoltWireConv *conv;
  gboolean as_str;

  g_return_val_if_fail (wire_type != NULL, FALSE);
  g_return_val_if_fail (prop_spec != NULL, FALSE);

  conv = g_new (BoltWireConv, 1);
  conv->ref_count = 1;
  conv->wire_type = g_variant_type_copy (wire_type);
  conv->prop_spec = g_param_spec_ref (prop_spec);

  as_str = g_variant_type_equal (wire_type, G_VARIANT_TYPE_STRING);

  if (as_str && G_IS_PARAM_SPEC_ENUM (prop_spec))
    {
      conv->conv_type = BOLT_WIRE_CONV_ENUM_AS_STRING;
      conv->to_wire = conv_enum_to_str;
      conv->from_wire = conv_enum_from_str;
    }
  else if (as_str && G_IS_PARAM_SPEC_FLAGS (prop_spec))
    {
      conv->conv_type = BOLT_WIRE_CONV_FLAGS_AS_STRING;
      conv->to_wire = conv_flags_to_str;
      conv->from_wire = conv_flags_from_str;
    }
  else if (as_str && G_IS_PARAM_SPEC_OBJECT (prop_spec))
    {
      conv->conv_type = BOLT_WIRE_CONV_OBJECT_AS_STRING;
      conv->to_wire = conv_obj_to_str;
      conv->from_wire = conv_obj_from_str;
    }
  else
    {
      conv->conv_type = BOLT_WIRE_CONV_NATIVE;
      conv->to_wire = conv_value_to_variant;
      conv->from_wire = conv_value_from_variant;
    }

  return conv;
}

GVariant *
bolt_wire_conv_to_wire (BoltWireConv *conv,
                        const GValue *value,
                        GError      **error)
{
  GVariant *res = NULL;

  g_return_val_if_fail (conv != NULL, NULL);
  g_return_val_if_fail (G_IS_VALUE (value), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  res = conv->to_wire (conv, value, error);

  if (res == NULL)
    return NULL;

  if (g_variant_is_floating (res))
    g_variant_ref_sink (res);

  return res;
}

gboolean
bolt_wire_conv_from_wire (BoltWireConv *conv,
                          GVariant     *wire,
                          GValue       *value,
                          GError      **error)
{
  gboolean ok;

  g_return_val_if_fail (conv != NULL, FALSE);
  g_return_val_if_fail (wire != NULL, FALSE);
  g_return_val_if_fail (G_IS_VALUE (value), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  ok = conv->from_wire (conv, wire, value, error);

  return ok;
}

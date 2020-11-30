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

#include <gio/gio.h>

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

gboolean
bolt_str_parse_by_pspec (GParamSpec *spec,
                         const char *str,
                         GValue     *val,
                         GError    **error)
{
  gboolean ok;

  if (val->g_type == 0)
    g_value_init (val, spec->value_type);

  g_return_val_if_fail (val->g_type == spec->value_type, FALSE);

  if (G_IS_PARAM_SPEC_BOOLEAN (spec))
    {
      gboolean v;

      ok = bolt_str_parse_as_boolean (str, &v, error);
      if (!ok)
        return FALSE;

      g_value_set_boolean (val, (gboolean) v);
    }
  else if (G_IS_PARAM_SPEC_UINT (spec))
    {
      GParamSpecUInt *s = G_PARAM_SPEC_UINT (spec);
      guint64 v;

      ok = bolt_str_parse_as_uint64 (str, &v, error);
      if (!ok)
        return FALSE;

      if (v < s->minimum || v > s->maximum)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                       "'%" G_GUINT64_FORMAT "' out of range for property",
                       v);
          return FALSE;
        }

      g_value_set_uint (val, (guint) v);
    }
  else if (G_IS_PARAM_SPEC_UINT64 (spec))
    {
      GParamSpecUInt64 *s = G_PARAM_SPEC_UINT64 (spec);
      guint64 v;

      ok = bolt_str_parse_as_uint64 (str, &v, error);
      if (!ok)
        return FALSE;

      if (v < s->minimum || v > s->maximum)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                       "'%" G_GUINT64_FORMAT "' out of range for property",
                       v);
          return FALSE;
        }

      g_value_set_uint64 (val, (guint64) v);
    }
  else if (G_IS_PARAM_SPEC_ENUM (spec))
    {
      GParamSpecEnum *s = G_PARAM_SPEC_ENUM (spec);
      gint v;

      ok = bolt_enum_class_from_string (s->enum_class, str, &v, error);
      if (!ok)
        return FALSE;

      g_value_set_enum (val, v);
    }
  else if (G_IS_PARAM_SPEC_FLAGS (spec))
    {
      GParamSpecFlags *s = G_PARAM_SPEC_FLAGS (spec);
      guint v;

      ok = bolt_flags_class_from_string (s->flags_class, str, &v, error);
      if (!ok)
        return FALSE;

      g_value_set_flags (val, v);
    }
  else if (G_IS_PARAM_SPEC_STRING (spec))
    {
      g_value_set_string (val, str);
    }
  else if (G_IS_PARAM_SPEC_BOXED (spec) && spec->value_type == G_TYPE_STRV)
    {
      g_auto(GStrv) strv = NULL;

      strv = g_strsplit (str, ",", -1);
      g_value_take_boxed (val, g_steal_pointer (&strv));
    }
  else
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "parsing of '%s' is not supported",
                   g_type_name (spec->value_type));
      return FALSE;
    }

  return TRUE;
}

GPtrArray *
bolt_properties_for_type (GType target)
{
  g_autofree GParamSpec **specs = NULL;
  GObjectClass *klass;
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
  BOLT_WIRE_CONV_CUSTOM,
  BOLT_WIRE_CONV_ENUM_AS_STRING,
  BOLT_WIRE_CONV_FLAGS_AS_STRING,
  BOLT_WIRE_CONV_OBJECT_AS_STRING,
} BoltWireConvType;

struct _BoltWireConv
{
  /* book-keeping */
  volatile gint ref_count;

  /*  */
  GVariantType *wire_type;
  GParamSpec   *prop_spec;

  /*  */
  BoltWireConvType conv_type;
  BoltConvToWire   to_wire;
  BoltConvFromWire from_wire;

  /*  */
  char *custom_id;
};

/* helper */
static inline gboolean
value_is_initialized (GValue *value)
{
  return G_VALUE_TYPE (value) != 0;
}

static void
wire_conv_init_value_if_needed (BoltWireConv *conv,
                                GValue       *value)
{
  if (!value_is_initialized (value))
    g_value_init (value, conv->prop_spec->value_type);
}

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
  char *str;
  guint uv;

  fs = G_PARAM_SPEC_FLAGS (conv->prop_spec);
  uv = g_value_get_flags (value);

  str = bolt_flags_class_to_string (fs->flags_class, uv, error);

  if (str == NULL)
    return NULL;

  return g_variant_new_take_string (str);
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
  GType want = value->g_type;

  g_dbus_gvariant_to_gvalue (wire, value);

  /* if the value was initialized before, make sure
   * we got the same type */
  if (want != 0 && value->g_type != want)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "Can not convert wire from %s to %s",
                   g_type_name (value->g_type), g_type_name (want));
      return FALSE;
    }

  return TRUE;
}

static GVariant *
conv_str_to_wire (BoltWireConv *conv,
                  const GValue *value,
                  GError      **error)
{
  const char *str = g_value_get_string (value);

  if (str == NULL)
    str = "";

  return g_variant_new_string (str);
}

static gboolean
conv_str_from_wire (BoltWireConv *conv,
                    GVariant     *wire,
                    GValue       *value,
                    GError      **error)
{
  const char *str = g_variant_get_string (wire, NULL);

  if (str && *str == '\0')
    str = NULL;

  g_value_set_static_string (value, str);

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
      g_clear_pointer (&conv->custom_id, g_free);

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

gboolean
bolt_wire_conv_is_native (BoltWireConv *conv)
{
  g_return_val_if_fail (conv != NULL, TRUE);

  return conv->conv_type == BOLT_WIRE_CONV_NATIVE;
}

const char *
bolt_wire_conv_describe (BoltWireConv *conv)
{
  g_return_val_if_fail (conv != NULL, "*invalid*");

  switch (conv->conv_type)
    {
    case BOLT_WIRE_CONV_NATIVE:
      return "native";

    case BOLT_WIRE_CONV_ENUM_AS_STRING:
      return "enum-as-string";

    case BOLT_WIRE_CONV_FLAGS_AS_STRING:
      return "flags-as-string";

    case BOLT_WIRE_CONV_OBJECT_AS_STRING:
      return "object-as-string";

    case BOLT_WIRE_CONV_CUSTOM:
      if (conv->custom_id)
        return conv->custom_id;
      return "custom";
    }

  return "*unknown*";
}

BoltWireConv *
bolt_wire_conv_for (const GVariantType *wire_type,
                    GParamSpec         *prop_spec)
{
  BoltWireConv *conv;
  gboolean as_str;

  g_return_val_if_fail (wire_type != NULL, NULL);
  g_return_val_if_fail (prop_spec != NULL, NULL);

  conv = g_new (BoltWireConv, 1);
  conv->ref_count = 1;
  conv->wire_type = g_variant_type_copy (wire_type);
  conv->prop_spec = g_param_spec_ref (prop_spec);
  conv->custom_id = NULL;

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
  else if (as_str && G_IS_PARAM_SPEC_STRING (prop_spec))
    {
      conv->conv_type = BOLT_WIRE_CONV_NATIVE;
      conv->to_wire = conv_str_to_wire;
      conv->from_wire = conv_str_from_wire;
    }
  else
    {
      conv->conv_type = BOLT_WIRE_CONV_NATIVE;
      conv->to_wire = conv_value_to_variant;
      conv->from_wire = conv_value_from_variant;
    }

  return conv;
}

BoltWireConv *
bolt_wire_conv_custom (const GVariantType *wire_type,
                       GParamSpec         *prop_spec,
                       const char         *custom_id,
                       BoltConvToWire      to_wire,
                       BoltConvFromWire    from_wire)
{
  BoltWireConv *conv;

  g_return_val_if_fail (wire_type != NULL, NULL);
  g_return_val_if_fail (prop_spec != NULL, NULL);

  g_return_val_if_fail (wire_type != NULL, NULL);
  g_return_val_if_fail (prop_spec != NULL, NULL);

  conv = g_new (BoltWireConv, 1);
  conv->ref_count = 1;

  conv->conv_type = BOLT_WIRE_CONV_CUSTOM;
  conv->wire_type = g_variant_type_copy (wire_type);
  conv->prop_spec = g_param_spec_ref (prop_spec);
  conv->custom_id = g_strdup (custom_id);

  conv->to_wire = to_wire;
  conv->from_wire = from_wire;

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
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  wire_conv_init_value_if_needed (conv, value);

  ok = conv->from_wire (conv, wire, value, error);

  return ok;
}

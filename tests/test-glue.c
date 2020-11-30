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

#include "bolt-error.h"
#include "bolt-wire.h"
#include "test-enums.h"
#include "bolt-test-resources.h"

#include <glib.h>
#include <gio/gio.h>
#include <glib/gprintf.h>

#include <locale.h>

/* *** Tiny object with only an "id" property (adapted from test-exported.c) */
#define BT_TYPE_ID bt_id_get_type ()
G_DECLARE_FINAL_TYPE (BtId, bt_id, BT, ID, GObject);

struct _BtId
{
  GObject parent;
};

G_DEFINE_TYPE (BtId, bt_id, G_TYPE_OBJECT);

enum {
  PROP_ID_0,
  PROP_ID_OID,
  PROP_ID_LAST
};


static GParamSpec *id_props[PROP_ID_LAST] = {NULL, };


static void
bt_id_init (BtId *bi)
{
}

static void
bt_id_get_property (GObject    *object,
                    guint       prop_id,
                    GValue     *value,
                    GParamSpec *pspec)
{
  switch (prop_id)
    {
    case PROP_ID_OID:
      g_value_set_string (value, "<no-id>");
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bt_id_class_init (BtIdClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = bt_id_get_property;

  id_props[PROP_ID_OID] =
    g_param_spec_string ("object-id",
                         NULL, NULL,
                         NULL,
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class,
                                     PROP_ID_LAST,
                                     id_props);


}


/* *** Glue test object */
#define BT_TYPE_GLUE bt_glue_get_type ()
G_DECLARE_FINAL_TYPE (BtGlue, bt_glue, BT, GLUE, BtId);

struct _BtGlue
{
  BtId parent;

};

G_DEFINE_TYPE (BtGlue, bt_glue, BT_TYPE_ID);

enum {
  PROP_GLUE_0,
  PROP_OBJECT_ID,
  PROP_ID,
  PROP_GLUE_LAST
};


static GParamSpec *glue_props[PROP_GLUE_LAST] = {NULL, };

static void
bt_glue_init (BtGlue *bg)
{
}

static void
bt_glue_get_property (GObject    *object,
                      guint       prop_id,
                      GValue     *value,
                      GParamSpec *pspec)
{
  switch (prop_id)
    {
    case PROP_ID:
    case PROP_OBJECT_ID:
      g_value_set_string (value, "bt-glue");
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bt_glue_class_init (BtGlueClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = bt_glue_get_property;

  glue_props[PROP_OBJECT_ID] =
    bolt_param_spec_override (gobject_class, "object-id");

  glue_props[PROP_ID] =
    g_param_spec_string ("id", "Id", NULL,
                         NULL,
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class,
                                     PROP_GLUE_LAST,
                                     glue_props);

}

typedef struct
{
  BtGlue *bg;
} TestGlue;

static void
test_glue_setup (TestGlue *tt, gconstpointer data)
{
  tt->bg = g_object_new (BT_TYPE_GLUE, NULL);
}

static void
test_glue_teardown (TestGlue *tt, gconstpointer data)
{
  g_clear_object (&tt->bg);
}

/* The tests */
static void
test_param_spec_override (TestGlue *tt, gconstpointer data)
{
  g_autofree char *oid = NULL;
  g_autofree char *id = NULL;

  g_object_get (tt->bg,
                "id", &id,
                "object-id", &oid,
                NULL);

  g_assert_nonnull (id);
  g_assert_nonnull (oid);
  g_assert_cmpstr (id, ==, oid);
}

static void
test_props_basic (TestGlue *tt, gconstpointer data)
{
  g_autoptr(GPtrArray) props = NULL;
  g_autoptr(GParamSpec) pspec = NULL;
  g_autoptr(GError) err = NULL;
  gboolean ok;

  props = bolt_properties_for_type (BT_TYPE_GLUE);

  g_assert_nonnull (props);
  g_assert_cmpuint (props->len, ==, PROP_GLUE_LAST - 2);

  ok = bolt_properties_find (props, "id", &pspec, &err);
  g_assert_no_error (err);
  g_assert_true (ok);
  g_assert_nonnull (pspec);

  g_assert_true (pspec == glue_props[PROP_ID]);
}

static void
test_parse_str_by_pspec_bool (TestGlue *tt, gconstpointer data)
{
  g_autoptr(GParamSpec) spec = NULL;
  g_autoptr(GError) err = NULL;
  g_auto(GValue) val = G_VALUE_INIT;
  gboolean ok;

  spec = g_param_spec_boolean ("Test", NULL, NULL, TRUE, G_PARAM_STATIC_STRINGS);

  /* ok: true */
  ok = bolt_str_parse_by_pspec (spec, "true", &val, &err);
  g_assert_no_error (err);
  g_assert_true (ok);
  g_assert_true (G_VALUE_HOLDS_BOOLEAN (&val));
  g_assert_true (g_value_get_boolean (&val));

  /* ok: false */
  g_value_reset (&val);
  ok = bolt_str_parse_by_pspec (spec, "false", &val, &err);
  g_assert_no_error (err);
  g_assert_true (ok);
  g_assert_true (G_VALUE_HOLDS_BOOLEAN (&val));
  g_assert_false (g_value_get_boolean (&val));

  /* error */
  g_value_unset (&val);
  ok = bolt_str_parse_by_pspec (spec, "narf", &val, &err);
  g_assert_nonnull (err);
  g_assert_false (ok);
  g_clear_error (&err);
}

static void
test_parse_str_by_pspec_uint (TestGlue *tt, gconstpointer data)
{
  g_autoptr(GParamSpec) spec = NULL;
  g_autoptr(GError) err = NULL;
  g_auto(GValue) val = G_VALUE_INIT;
  gboolean ok;

  spec = g_param_spec_uint ("UInt", NULL, NULL,
                            10, 100, 11,
                            G_PARAM_STATIC_STRINGS);

  /* ok: 10 */
  ok = bolt_str_parse_by_pspec (spec, "10", &val, &err);
  g_assert_no_error (err);
  g_assert_true (ok);
  g_assert_true (G_VALUE_HOLDS_UINT (&val));
  g_assert_cmpuint (g_value_get_uint (&val), ==, 10);

  /* ok: false */
  g_value_reset (&val);
  ok = bolt_str_parse_by_pspec (spec, "0x2A", &val, &err);
  g_assert_no_error (err);
  g_assert_true (ok);
  g_assert_true (G_VALUE_HOLDS_UINT (&val));
  g_assert_cmpuint (g_value_get_uint (&val), ==, 0x2A);

  /* error: out of bounds */
  g_value_unset (&val);
  ok = bolt_str_parse_by_pspec (spec, "111", &val, &err);
  g_assert_nonnull (err);
  g_assert_false (ok);
  g_clear_error (&err);

  /* error: not a number */
  g_value_unset (&val);
  ok = bolt_str_parse_by_pspec (spec, "narf", &val, &err);
  g_assert_nonnull (err);
  g_assert_false (ok);
  g_clear_error (&err);
}

static void
test_parse_str_by_pspec_uint64 (TestGlue *tt, gconstpointer data)
{
  g_autoptr(GParamSpec) spec = NULL;
  g_autoptr(GError) err = NULL;
  g_auto(GValue) val = G_VALUE_INIT;
  gboolean ok;

  spec = g_param_spec_uint64 ("UInt64", NULL, NULL,
                              10, 100, 11,
                              G_PARAM_STATIC_STRINGS);

  /* ok: 10 */
  ok = bolt_str_parse_by_pspec (spec, "10", &val, &err);
  g_assert_no_error (err);
  g_assert_true (ok);
  g_assert_true (G_VALUE_HOLDS_UINT64 (&val));
  g_assert_cmpuint (g_value_get_uint64 (&val), ==, 10);

  /* ok: false */
  g_value_reset (&val);
  ok = bolt_str_parse_by_pspec (spec, "0x2A", &val, &err);
  g_assert_no_error (err);
  g_assert_true (ok);
  g_assert_true (G_VALUE_HOLDS_UINT64 (&val));
  g_assert_cmpuint (g_value_get_uint64 (&val), ==, 0x2A);

  /* error: out of bounds */
  g_value_unset (&val);
  ok = bolt_str_parse_by_pspec (spec, "111", &val, &err);
  g_assert_nonnull (err);
  g_assert_false (ok);
  g_clear_error (&err);

  /* error: not a number */
  g_value_unset (&val);
  ok = bolt_str_parse_by_pspec (spec, "narf", &val, &err);
  g_assert_nonnull (err);
  g_assert_false (ok);
  g_clear_error (&err);
}

static void
test_parse_str_by_pspec_enum (TestGlue *tt, gconstpointer data)
{
  g_autoptr(GParamSpec) spec = NULL;
  g_autoptr(GError) err = NULL;
  g_auto(GValue) val = G_VALUE_INIT;
  gboolean ok;

  spec = g_param_spec_enum ("Enum", NULL, NULL,
                            BOLT_TYPE_TEST_ENUM,
                            BOLT_TEST_ONE,
                            G_PARAM_STATIC_STRINGS);

  /* ok: valid enum value */
  ok = bolt_str_parse_by_pspec (spec, "two", &val, &err);
  g_assert_no_error (err);
  g_assert_true (ok);
  g_assert_true (G_VALUE_HOLDS_ENUM (&val));
  g_assert_cmpint (g_value_get_enum (&val), ==, BOLT_TEST_TWO);

  /* ok: valid enum value */
  g_value_unset (&val);
  ok = bolt_str_parse_by_pspec (spec, "unknown", &val, &err);
  g_assert_no_error (err);
  g_assert_true (ok);
  g_assert_true (G_VALUE_HOLDS_ENUM (&val));
  g_assert_cmpint (g_value_get_enum (&val), ==, BOLT_TEST_UNKNOWN);

  /* error: invalid value */
  g_value_unset (&val);
  ok = bolt_str_parse_by_pspec (spec, "six", &val, &err);
  g_assert_nonnull (err);
  g_assert_false (ok);
  g_clear_error (&err);
}

static void
test_parse_str_by_pspec_flags (TestGlue *tt, gconstpointer data)
{
  g_autoptr(GParamSpec) spec = NULL;
  g_autoptr(GError) err = NULL;
  g_auto(GValue) val = G_VALUE_INIT;
  gboolean ok;

  spec = g_param_spec_flags ("Flags", NULL, NULL,
                             BOLT_TYPE_KITT_FLAGS,
                             BOLT_KITT_DEFAULT,
                             G_PARAM_STATIC_STRINGS);

  /* ok: valid enum value */
  ok = bolt_str_parse_by_pspec (spec, "enabled", &val, &err);
  g_assert_no_error (err);
  g_assert_true (ok);
  g_assert_true (G_VALUE_HOLDS_FLAGS (&val));
  g_assert_cmpuint (g_value_get_flags (&val), ==, BOLT_KITT_ENABLED);

  /* ok: valid enum value */
  g_value_unset (&val);
  ok = bolt_str_parse_by_pspec (spec, "sspm|turbo-boost", &val, &err);
  g_assert_no_error (err);
  g_assert_true (ok);
  g_assert_true (G_VALUE_HOLDS_FLAGS (&val));
  g_assert_cmpuint (g_value_get_flags (&val),
                    ==,
                    BOLT_KITT_SSPM |
                    BOLT_KITT_TURBO_BOOST);

  /* error: invalid value */
  g_value_unset (&val);
  ok = bolt_str_parse_by_pspec (spec, "six", &val, &err);
  g_assert_nonnull (err);
  g_assert_false (ok);
  g_clear_error (&err);
}

static void
test_parse_str_by_pspec_string (TestGlue *tt, gconstpointer data)
{
  g_autoptr(GParamSpec) spec = NULL;
  g_autoptr(GError) err = NULL;
  g_auto(GValue) val = G_VALUE_INIT;
  gboolean ok;

  spec = g_param_spec_string ("String", NULL, NULL,
                              "default",
                              G_PARAM_STATIC_STRINGS);

  /* ok: valid string */
  ok = bolt_str_parse_by_pspec (spec, "enabled", &val, &err);
  g_assert_no_error (err);
  g_assert_true (ok);
  g_assert_true (G_VALUE_HOLDS_STRING (&val));
  g_assert_cmpstr (g_value_get_string (&val), ==, "enabled");
}

static void
test_parse_str_by_pspec_strv (TestGlue *tt, gconstpointer data)
{
  g_autoptr(GParamSpec) spec = NULL;
  g_autoptr(GError) err = NULL;
  g_auto(GValue) val = G_VALUE_INIT;
  char **strv = NULL;
  gboolean ok;

  spec = g_param_spec_boxed ("StringVector", NULL, NULL,
                             G_TYPE_STRV,
                             G_PARAM_STATIC_STRINGS);

  /* ok: valid enum value */
  ok = bolt_str_parse_by_pspec (spec, "a,b,c", &val, &err);
  g_assert_no_error (err);
  g_assert_true (ok);
  g_assert_true (G_VALUE_HOLDS_BOXED (&val));
  strv = g_value_get_boxed (&val);
  g_assert_cmpuint (g_strv_length (strv), ==, 3);
  g_assert_cmpstr (strv[0], ==, "a");
  g_assert_cmpstr (strv[1], ==, "b");
  g_assert_cmpstr (strv[2], ==, "c");
}


static void
test_wire_conv_enum (TestGlue *tt, gconstpointer data)
{
  g_autoptr(BoltWireConv) conv = NULL;
  g_autoptr(GParamSpec) spec = NULL;
  g_autoptr(GError) err = NULL;
  g_autoptr(GVariant) bogus = NULL;
  g_autoptr(GVariant) var = NULL;
  g_auto(GValue) val = G_VALUE_INIT;
  const GVariantType *wire_type;
  const GParamSpec *prop_spec;
  gboolean ok;

  spec = g_param_spec_enum ("test", "Test",
                            "Test Enumeration",
                            BOLT_TYPE_TEST_ENUM,
                            BOLT_TEST_TWO,
                            G_PARAM_READWRITE |
                            G_PARAM_STATIC_STRINGS);

  conv = bolt_wire_conv_for (G_VARIANT_TYPE_STRING, spec);

  g_assert_nonnull (conv);

  wire_type = bolt_wire_conv_get_wire_type (conv);
  prop_spec = bolt_wire_conv_get_prop_spec (conv);

  g_assert_false (bolt_wire_conv_is_native (conv));
  g_assert_nonnull (bolt_wire_conv_describe (conv));

  g_assert_cmpstr ((const char *) wire_type,
                   ==,
                   (const char *) G_VARIANT_TYPE_STRING);

  g_assert_true (prop_spec == spec);

  g_value_init (&val, BOLT_TYPE_TEST_ENUM);
  g_value_set_enum (&val, BOLT_TEST_THREE);

  /* to the wire */
  var = bolt_wire_conv_to_wire (conv, &val, &err);
  g_assert_no_error (err);
  g_assert_nonnull (var);

  g_assert_cmpstr (g_variant_get_string (var, NULL),
                   ==,
                   "three");

  /* from the wire, value is unset */
  g_value_unset (&val);
  g_assert_true (G_VALUE_TYPE (&val) == 0);

  ok = bolt_wire_conv_from_wire (conv, var, &val, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  g_assert_cmpint (g_value_get_enum (&val),
                   ==,
                   BOLT_TEST_THREE);

  /* from the wire, value is preset */
  g_value_reset (&val);
  g_assert_true (G_VALUE_HOLDS (&val, BOLT_TYPE_TEST_ENUM));

  ok = bolt_wire_conv_from_wire (conv, var, &val, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  g_assert_cmpint (g_value_get_enum (&val),
                   ==,
                   BOLT_TEST_THREE);

  /* values from the wire can not be trusted */
  g_value_reset (&val);
  g_assert_cmpint (g_value_get_enum (&val), !=, BOLT_TEST_THREE);
  g_assert_cmpint (g_value_get_enum (&val), !=, BOLT_TEST_TWO);

  bogus = g_variant_ref_sink (g_variant_new_string ("bogus-bogus"));
  ok = bolt_wire_conv_from_wire (conv, bogus, &val, &err);
  g_assert_nonnull (err);
  g_assert_false (ok);
  g_assert_cmpint (g_value_get_enum (&val),
                   ==,
                   G_PARAM_SPEC_ENUM (spec)->default_value);
}

static void
test_wire_conv_flags (TestGlue *tt, gconstpointer data)
{
  g_autoptr(BoltWireConv) conv = NULL;
  g_autoptr(GParamSpec) spec = NULL;
  g_autoptr(GError) err = NULL;
  g_autoptr(GVariant) bogus = NULL;
  g_autoptr(GVariant) var = NULL;
  g_auto(GValue) val = G_VALUE_INIT;
  const GVariantType *wire_type;
  const GParamSpec *prop_spec;
  gboolean ok;

  spec = g_param_spec_flags ("test", "Test",
                             "Test Flags",
                             BOLT_TYPE_KITT_FLAGS,
                             BOLT_KITT_DEFAULT,
                             G_PARAM_READWRITE |
                             G_PARAM_STATIC_STRINGS);

  conv = bolt_wire_conv_for (G_VARIANT_TYPE_STRING, spec);

  g_assert_nonnull (conv);

  wire_type = bolt_wire_conv_get_wire_type (conv);
  prop_spec = bolt_wire_conv_get_prop_spec (conv);

  g_assert_false (bolt_wire_conv_is_native (conv));
  g_assert_nonnull (bolt_wire_conv_describe (conv));

  g_assert_cmpstr ((const char *) wire_type,
                   ==,
                   (const char *) G_VARIANT_TYPE_STRING);

  g_assert_true (prop_spec == spec);

  g_value_init (&val, BOLT_TYPE_KITT_FLAGS);
  g_value_set_flags (&val, BOLT_KITT_ENABLED);

  /* to the wire */
  var = bolt_wire_conv_to_wire (conv, &val, &err);
  g_assert_no_error (err);
  g_assert_nonnull (var);

  g_assert_cmpstr (g_variant_get_string (var, NULL),
                   ==,
                   "enabled");

  /* from the wire, value is unset */
  g_value_unset (&val);
  g_assert_true (G_VALUE_TYPE (&val) == 0);

  ok = bolt_wire_conv_from_wire (conv, var, &val, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  g_assert_cmpint (g_value_get_flags (&val),
                   ==,
                   BOLT_KITT_ENABLED);

  /* from the wire, value is preset */
  g_value_reset (&val);
  g_assert_true (G_VALUE_HOLDS (&val, BOLT_TYPE_KITT_FLAGS));

  ok = bolt_wire_conv_from_wire (conv, var, &val, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  g_assert_cmpint (g_value_get_flags (&val),
                   ==,
                   BOLT_KITT_ENABLED);

  /* values from the wire can not be trusted */
  g_value_reset (&val);

  bogus = g_variant_ref_sink (g_variant_new_string ("bogus-bogus"));
  ok = bolt_wire_conv_from_wire (conv, bogus, &val, &err);
  g_assert_nonnull (err);
  g_assert_false (ok);
  g_assert_cmpint (g_value_get_flags (&val),
                   ==,
                   G_PARAM_SPEC_FLAGS (spec)->default_value);
}

static void
test_wire_conv_object (TestGlue *tt, gconstpointer data)
{
  g_autoptr(BoltWireConv) conv = NULL;
  g_autoptr(GParamSpec) spec = NULL;
  g_autoptr(GError) err = NULL;
  g_autoptr(GVariant) bogus = NULL;
  g_autoptr(GVariant) var = NULL;
  g_auto(GValue) val = G_VALUE_INIT;
  const GVariantType *wire_type;
  const GParamSpec *prop_spec;
  gboolean ok;

  spec = g_param_spec_object ("obj", "Obj",
                              "Object Test",
                              BT_TYPE_GLUE,
                              G_PARAM_STATIC_STRINGS);

  conv = bolt_wire_conv_for (G_VARIANT_TYPE_STRING, spec);

  g_assert_nonnull (conv);

  wire_type = bolt_wire_conv_get_wire_type (conv);
  prop_spec = bolt_wire_conv_get_prop_spec (conv);

  g_assert_false (bolt_wire_conv_is_native (conv));
  g_assert_nonnull (bolt_wire_conv_describe (conv));

  g_assert_cmpstr ((const char *) wire_type,
                   ==,
                   (const char *) G_VARIANT_TYPE_STRING);

  g_assert_true (prop_spec == spec);

  /* to the wire, empty value (empty prop), which is legal */
  g_value_init (&val, BT_TYPE_GLUE);
  g_value_set_object (&val, NULL);

  var = bolt_wire_conv_to_wire (conv, &val, &err);
  g_assert_no_error (err);
  g_assert_nonnull (var);

  g_assert_cmpstr (g_variant_get_string (var, NULL),
                   ==,
                   "");

  g_clear_pointer (&var, g_variant_unref);
  /* to the wire, value holding a valid object */
  g_value_reset (&val);
  g_value_set_object (&val, tt->bg);

  var = bolt_wire_conv_to_wire (conv, &val, &err);
  g_assert_no_error (err);
  g_assert_nonnull (var);

  g_assert_cmpstr (g_variant_get_string (var, NULL),
                   ==,
                   "bt-glue");

  /* the other way around does not work */
  g_value_reset (&val);
  bogus = g_variant_ref_sink (g_variant_new_string ("bt-glue"));
  ok = bolt_wire_conv_from_wire (conv, bogus, &val, &err);
  g_assert_nonnull (err);
  g_assert_false (ok);
}

static void
test_wire_conv_simple (TestGlue *tt, gconstpointer data)
{
  g_autoptr(BoltWireConv) conv = NULL;
  g_autoptr(GError) err = NULL;
  g_autoptr(GVariant) var = NULL;
  g_autoptr(GParamSpec) spec = NULL;
  g_auto(GValue) val = G_VALUE_INIT;
  const GVariantType *wire_type;
  const GParamSpec *prop_spec;
  gboolean ok;

  spec = g_param_spec_uint64 ("uint", "Uint",
                              "Unsigned Integer",
                              0, 100, 23,
                              G_PARAM_STATIC_STRINGS);

  conv = bolt_wire_conv_for (G_VARIANT_TYPE_UINT64, spec);

  g_assert_nonnull (conv);

  wire_type = bolt_wire_conv_get_wire_type (conv);
  prop_spec = bolt_wire_conv_get_prop_spec (conv);

  g_assert_true (bolt_wire_conv_is_native (conv));
  g_assert_nonnull (bolt_wire_conv_describe (conv));

  g_assert_cmpstr ((const char *) wire_type,
                   ==,
                   (const char *) G_VARIANT_TYPE_UINT64);

  g_assert_true (prop_spec == spec);

  g_value_init (&val, G_TYPE_UINT64);
  g_value_set_uint64 (&val, 42U);

  /* to the wire */
  var = bolt_wire_conv_to_wire (conv, &val, &err);
  g_assert_no_error (err);
  g_assert_nonnull (var);

  g_assert_cmpuint (g_variant_get_uint64 (var),
                    ==,
                    42U);

  /* from the wire, value is unset */
  g_value_unset (&val);
  g_assert_true (G_VALUE_TYPE (&val) == 0);

  ok = bolt_wire_conv_from_wire (conv, var, &val, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  g_assert_cmpint (g_value_get_uint64 (&val),
                   ==,
                   42U);

  /* from the wire, value is preset */
  g_value_reset (&val);
  g_assert_true (G_VALUE_HOLDS (&val, G_TYPE_UINT64));
  g_assert_cmpuint (g_value_get_uint64 (&val), !=, 42U);

  ok = bolt_wire_conv_from_wire (conv, var, &val, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  g_assert_cmpint (g_value_get_uint64 (&val),
                   ==,
                   42U);
}

static void
test_wire_conv_custom (TestGlue *tt, gconstpointer data)
{
  g_autoptr(BoltWireConv) conv = NULL;
  g_autoptr(GParamSpec) spec = NULL;
  g_autoptr(GError) err = NULL;
  g_autoptr(GVariant) var = NULL;
  g_auto(GValue) val = G_VALUE_INIT;
  gboolean ok;
  BoltLinkSpeed *check;
  BoltLinkSpeed attr =
  {.rx.speed = 10,
   .rx.lanes = 1,
   .tx.speed = 20,
   .tx.lanes = 2};

  spec = g_param_spec_boxed ("link-speed", "LinkSpeed",
                             "Link Speed Info",
                             BOLT_TYPE_LINK_SPEED,
                             G_PARAM_STATIC_STRINGS);

  conv = bolt_wire_conv_custom (G_VARIANT_TYPE ("a{su}"), spec,
                                "link speed to dict",
                                bolt_link_speed_to_wire,
                                bolt_link_speed_from_wire);

  g_assert_nonnull (conv);

  g_assert_false (bolt_wire_conv_is_native (conv));
  g_assert_nonnull (bolt_wire_conv_describe (conv));

  g_value_init (&val, BOLT_TYPE_LINK_SPEED);
  g_value_set_boxed (&val, &attr);

  /* to the wire */
  var = bolt_wire_conv_to_wire (conv, &val, &err);
  g_assert_no_error (err);
  g_assert_nonnull (var);

  /* from the wire, value is unset */
  g_value_unset (&val);
  g_assert_true (G_VALUE_TYPE (&val) == 0);

  ok = bolt_wire_conv_from_wire (conv, var, &val, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  check = g_value_get_boxed (&val);

  g_assert_cmpuint (attr.rx.speed, ==, check->rx.speed);
  g_assert_cmpuint (attr.rx.lanes, ==, check->rx.lanes);
  g_assert_cmpuint (attr.tx.speed, ==, check->tx.speed);
  g_assert_cmpuint (attr.tx.lanes, ==, check->tx.lanes);
}

int
main (int argc, char **argv)
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  g_test_add ("/common/param_spec_override",
              TestGlue,
              NULL,
              test_glue_setup,
              test_param_spec_override,
              test_glue_teardown);

  g_test_add ("/common/props_basic",
              TestGlue,
              NULL,
              test_glue_setup,
              test_props_basic,
              test_glue_teardown);

  g_test_add ("/common/str_parse_by_pspec/bool",
              TestGlue,
              NULL,
              NULL,
              test_parse_str_by_pspec_bool,
              NULL);

  g_test_add ("/common/str_parse_by_pspec/uint",
              TestGlue,
              NULL,
              NULL,
              test_parse_str_by_pspec_uint,
              NULL);

  g_test_add ("/common/str_parse_by_pspec/uint64",
              TestGlue,
              NULL,
              NULL,
              test_parse_str_by_pspec_uint64,
              NULL);

  g_test_add ("/common/str_parse_by_pspec/enum",
              TestGlue,
              NULL,
              NULL,
              test_parse_str_by_pspec_enum,
              NULL);

  g_test_add ("/common/str_parse_by_pspec/flags",
              TestGlue,
              NULL,
              NULL,
              test_parse_str_by_pspec_flags,
              NULL);

  g_test_add ("/common/str_parse_by_pspec/string",
              TestGlue,
              NULL,
              NULL,
              test_parse_str_by_pspec_string,
              NULL);

  g_test_add ("/common/str_parse_by_pspec/strv",
              TestGlue,
              NULL,
              NULL,
              test_parse_str_by_pspec_strv,
              NULL);

  g_test_add ("/common/wire_conv/enum",
              TestGlue,
              NULL,
              NULL,
              test_wire_conv_enum,
              NULL);

  g_test_add ("/common/wire_conv/flags",
              TestGlue,
              NULL,
              NULL,
              test_wire_conv_flags,
              NULL);

  g_test_add ("/common/wire_conv/object",
              TestGlue,
              NULL,
              test_glue_setup,
              test_wire_conv_object,
              test_glue_teardown);

  g_test_add ("/common/wire_conv/simple",
              TestGlue,
              NULL,
              NULL,
              test_wire_conv_simple,
              NULL);

  g_test_add ("/common/wire_conv/custom",
              TestGlue,
              NULL,
              NULL,
              test_wire_conv_custom,
              NULL);


  return g_test_run ();
}

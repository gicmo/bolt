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

  return g_test_run ();
}

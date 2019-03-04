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

#include "config.h"

#include "bolt-exported.h"

#include "bolt-enums.h"
#include "bolt-error.h"
#include "bolt-glue.h"
#include "bolt-str.h"

#include "test-enums.h"
#include "bolt-test-resources.h"

#include <glib.h>
#include <gio/gio.h>
#include <glib/gprintf.h>

#include <locale.h>

/* *** Tiny object with only an "id" property */
#define BT_TYPE_ID bt_id_get_type ()
G_DECLARE_FINAL_TYPE (BtId, bt_id, BT, ID, BoltExported);

struct _BtId
{
  BoltExported parent;
};

G_DEFINE_TYPE (BtId, bt_id, BOLT_TYPE_EXPORTED);

enum {
  PROP_ID_0,
  PROP_OBJECT_ID,
  PROP_ID,
  PROP_ID_LAST
};


static GParamSpec *id_props[PROP_ID_LAST] = {NULL, };


static void
bt_id_init (BtId *be)
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
    case PROP_ID:
    case PROP_OBJECT_ID:
      g_value_set_string (value, "bolt-id");
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

  id_props[PROP_OBJECT_ID] =
    bolt_param_spec_override (gobject_class,
                              "object-id");

  id_props[PROP_ID] =
    g_param_spec_string ("id", "Id", NULL,
                         NULL,
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_NICK |
                         G_PARAM_STATIC_BLURB);

  g_object_class_install_properties (gobject_class,
                                     PROP_ID_LAST,
                                     id_props);


}

/*  */
#define DBUS_IFACE "org.gnome.bolt.Example"
#define DBUS_OPATH_BASE "/bolt/test"

#define BT_TYPE_EXPORTED bt_exported_get_type ()
G_DECLARE_FINAL_TYPE (BtExported, bt_exported, BT, EXPORTED, BoltExported);

static GVariant *  handle_ping (BoltExported          *obj,
                                GVariant              *params,
                                GDBusMethodInvocation *inv,
                                GError               **error);

static GVariant *  handle_peng (BoltExported          *obj,
                                GVariant              *params,
                                GDBusMethodInvocation *inv,
                                GError               **error);

static gboolean handle_authorize_method (BoltExported          *exported,
                                         GDBusMethodInvocation *inv,
                                         GError               **error,
                                         gpointer               user_data);

static gboolean handle_authorize_property (BoltExported          *exported,
                                           const char            *name,
                                           gboolean               setting,
                                           GDBusMethodInvocation *invocation,
                                           GError               **error,
                                           gpointer               user_data);

static gboolean handle_set_str_rw (BoltExported *obj,
                                   const char   *name,
                                   const GValue *value,
                                   GError      **error);

static gboolean handle_set_security (BoltExported *obj,
                                     const char   *name,
                                     const GValue *value,
                                     GError      **error);

static gboolean handle_set_kitt (BoltExported *obj,
                                 const char   *name,
                                 const GValue *value,
                                 GError      **error);

struct _BtExported
{
  BoltExported  parent;

  char         *object_id;

  char         *str;
  GError       *setter_err;

  gboolean      prop_bool;

  BtId         *prop_obj;

  gboolean      authorize_methods;
  gboolean      authorize_properties;

  BoltSecurity  security;
  BoltKittFlags kitt;
};

G_DEFINE_TYPE (BtExported, bt_exported, BOLT_TYPE_EXPORTED);

enum {
  PROP_0,

  PROP_MYID,

  PROP_STR,
  PROP_STR_RW,
  PROP_STR_RW_NOSETTER,

  PROP_BOOL,
  PROP_OBJECT,

  PROP_SECURITY,
  PROP_KITT,

  PROP_LAST
};

static GParamSpec *props[PROP_LAST] = {NULL, };

static void
bt_exported_finalize (GObject *object)
{
  BtExported *be = BT_EXPORTED (object);

  g_clear_object (&be->prop_obj);

  g_free (be->str);
  g_free (be->object_id);


  G_OBJECT_CLASS (bt_exported_parent_class)->finalize (object);
}

static void
bt_exported_init (BtExported *be)
{
  be->object_id = g_strdup ("bt_exported0");
  be->str = g_strdup ("strfoo");
  be->prop_obj = g_object_new (BT_TYPE_ID, NULL);
}

static void
bt_exported_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  BtExported *be = BT_EXPORTED (object);

  switch (prop_id)
    {
    case PROP_MYID:
      g_value_set_string (value, be->object_id);
      break;

    case PROP_STR:
    case PROP_STR_RW:
    case PROP_STR_RW_NOSETTER:
      g_value_set_string (value, be->str);
      break;

    case PROP_BOOL:
      g_value_set_boolean (value, be->prop_bool);
      break;

    case PROP_OBJECT:
      g_value_set_object (value, be->prop_obj);
      break;

    case PROP_SECURITY:
      g_value_set_enum (value, be->security);
      break;

    case PROP_KITT:
      g_value_set_flags (value, be->kitt);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bt_exported_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  BtExported *be = BT_EXPORTED (object);

  switch (prop_id)
    {
    case PROP_STR:
    case PROP_STR_RW:
    case PROP_STR_RW_NOSETTER:
      g_clear_pointer (&be->str, g_free);
      be->str = g_value_dup_string (value);
      break;

    case PROP_BOOL:
      be->prop_bool = g_value_get_boolean (value);
      break;

    case PROP_SECURITY:
      be->security = g_value_get_enum (value);
      break;

    case PROP_KITT:
      be->kitt = g_value_get_flags (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}
static void
bt_exported_class_init (BtExportedClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  BoltExportedClass *exported_class = BOLT_EXPORTED_CLASS (klass);

  gobject_class->finalize = bt_exported_finalize;
  gobject_class->get_property = bt_exported_get_property;
  gobject_class->set_property = bt_exported_set_property;

  bolt_exported_class_set_interface_info (exported_class,
                                          DBUS_IFACE,
                                          "/bolt/tests/exported/example.bolt.xml");

  bolt_exported_class_set_object_path (exported_class, DBUS_OPATH_BASE);

  props[PROP_MYID] =
    bolt_param_spec_override (gobject_class, "object-id");

  props[PROP_STR] =
    g_param_spec_string ("str", "StrFoo", NULL,
                         NULL,
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_NICK |
                         G_PARAM_STATIC_BLURB);

  props[PROP_STR_RW] =
    g_param_spec_string ("str-rw", "StrRW", NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_NICK |
                         G_PARAM_STATIC_BLURB);

  props[PROP_STR_RW_NOSETTER] =
    g_param_spec_string ("str-rw-nosetter", "StrRWNoSetter", NULL,
                         NULL,
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_NICK |
                         G_PARAM_STATIC_BLURB);

  props[PROP_BOOL] =
    g_param_spec_boolean ("bool", "Bool", NULL,
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_NICK |
                          G_PARAM_STATIC_BLURB);

  props[PROP_OBJECT] =
    g_param_spec_object ("object", "Object", NULL,
                         BT_TYPE_ID,
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_NICK |
                         G_PARAM_STATIC_BLURB);

  props[PROP_SECURITY] =
    g_param_spec_enum ("security", "Security", NULL,
                       BOLT_TYPE_SECURITY,
                       BOLT_SECURITY_UNKNOWN,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_NICK |
                       G_PARAM_STATIC_BLURB);

  props[PROP_KITT] =
    g_param_spec_flags ("kitt", "KittMode", NULL,
                        BOLT_TYPE_KITT_FLAGS,
                        BOLT_KITT_DISABLED,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_NICK |
                        G_PARAM_STATIC_BLURB);

  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     props);

  bolt_exported_class_export_properties (exported_class, PROP_STR, PROP_LAST, props);
  bolt_exported_class_export_method (exported_class, "Ping", handle_ping);
  bolt_exported_class_export_method (exported_class, "Peng", handle_peng);

  bolt_exported_class_property_setter (exported_class,
                                       props[PROP_STR_RW],
                                       handle_set_str_rw);

  bolt_exported_class_property_setter (exported_class,
                                       props[PROP_SECURITY],
                                       handle_set_security);

  bolt_exported_class_property_setter (exported_class,
                                       props[PROP_KITT],
                                       handle_set_kitt);
}

static gboolean
handle_authorize_method (BoltExported          *exported,
                         GDBusMethodInvocation *inv,
                         GError               **error,
                         gpointer               user_data)
{
  BtExported *be = BT_EXPORTED (user_data);
  gboolean authorize = be->authorize_methods;
  const char *name = g_dbus_method_invocation_get_method_name (inv);

  g_debug ("authorizing method %s (%s)", name, authorize ? "y" : "n" );

  if (!authorize)
    g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED,
                 "denying property write access for %s", name);
  return authorize;

}

static gboolean
handle_authorize_property (BoltExported          *exported,
                           const char            *name,
                           gboolean               setting,
                           GDBusMethodInvocation *inv,
                           GError               **error,
                           gpointer               user_data)
{
  BtExported *be = BT_EXPORTED (user_data);
  gboolean authorize = be->authorize_properties;

  g_debug ("authorizing property %s (%s)", name, authorize ? "y" : "n" );

  if (!authorize)
    g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED,
                 "denying property write access for %s", name);
  return authorize;
}

static void
bt_exported_install_method_authorizer (BtExported *be)
{
  g_signal_connect (be, "authorize-method",
                    G_CALLBACK (handle_authorize_method),
                    be);
}


static void
bt_exported_install_property_authorizer (BtExported *be)
{
  g_signal_connect (be, "authorize-property",
                    G_CALLBACK (handle_authorize_property),
                    be);
}


static GVariant *
handle_ping (BoltExported          *obj,
             GVariant              *params,
             GDBusMethodInvocation *inv,
             GError               **error)
{
  //  BtExported *be = BT_EXPORTED (obj);
  return g_variant_new ("(s)", "PONG");
}

static GVariant *
handle_peng (BoltExported          *obj,
             GVariant              *params,
             GDBusMethodInvocation *inv,
             GError               **error)
{
  const char *str;

  g_variant_get_child (params, 0, "&s", &str);
  if (str == NULL)
    str = "PENG";

  g_set_error (error, BOLT_ERROR, BOLT_ERROR_FAILED,
               "failing with: %s", str);

  return NULL;
}


static gboolean
handle_set_str_rw (BoltExported *obj,
                   const char   *name,
                   const GValue *value,
                   GError      **error)
{
  BtExported *be = BT_EXPORTED (obj);

  g_printerr ("handling set str-rw\n");
  if (be->setter_err)
    {
      g_printerr ("signaling error\n");
      //*error = g_error_copy (be->setter_err);
      g_set_error_literal (error,
                           be->setter_err->domain,
                           be->setter_err->code,
                           be->setter_err->message);
      return FALSE;
    }

  g_clear_pointer (&be->str, g_free);
  be->str = g_value_dup_string (value);

  return TRUE;
}

static gboolean
handle_set_security (BoltExported *obj,
                     const char   *name,
                     const GValue *value,
                     GError      **error)
{
  BtExported *be = BT_EXPORTED (obj);

  be->security = g_value_get_enum (value);
  return TRUE;
}

static gboolean
handle_set_kitt (BoltExported *obj,
                 const char   *name,
                 const GValue *value,
                 GError      **error)
{
  BtExported *be = BT_EXPORTED (obj);

  be->kitt = g_value_get_flags (value);
  return TRUE;
}

/* *********** */

static GTestDBus *test_bus;

typedef struct
{
  GDBusConnection *bus;
  BtExported      *obj;

  const char      *bus_name;
  const char      *obj_path;
} TestExported;

static void
test_exported_setup (TestExported *tt, gconstpointer data)
{
  g_autoptr(GError) err = NULL;
  g_autofree char *opath = NULL;
  const char *obj_path = "/obj";
  gboolean exported = FALSE;
  gboolean ok;

  tt->bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &err);

  g_assert_no_error (err);
  g_assert_nonnull (tt->bus);
  g_assert_false (g_dbus_connection_is_closed (tt->bus));

  tt->obj = g_object_new (BT_TYPE_EXPORTED, NULL);
  g_assert_nonnull (tt->obj);

  ok = bolt_exported_export (BOLT_EXPORTED (tt->obj),
                             tt->bus,
                             obj_path,
                             &err);

  g_assert_no_error (err);
  g_assert_true (ok);

  tt->obj_path = bolt_exported_get_object_path (BOLT_EXPORTED (tt->obj));

  g_assert_cmpstr (tt->obj_path, ==, obj_path);
  tt->bus_name = g_dbus_connection_get_unique_name (tt->bus);

  g_object_get (tt->obj,
                "object-path", &opath,
                "exported", &exported,
                NULL);

  g_assert_cmpstr (obj_path, ==, opath);
  g_assert_true (exported);
  g_assert_true (bolt_exported_is_exported (BOLT_EXPORTED (tt->obj)));
}

static void
test_exported_teardown (TestExported *tt, gconstpointer data)
{
  gboolean ok;

  ok = bolt_exported_unexport (BOLT_EXPORTED (tt->obj));

  g_assert_true (ok);
  g_assert_false (bolt_exported_is_exported (BOLT_EXPORTED (tt->obj)));

  g_clear_object (&tt->bus);
  g_clear_object (&tt->obj);
}

typedef struct CallCtx
{
  GMainLoop *loop;
  GVariant  *data;
  GError    *error;
} CallCtx;

static CallCtx *
call_ctx_new (void)
{
  CallCtx *ctx = g_new0 (CallCtx, 1);

  ctx->loop = g_main_loop_new (NULL, FALSE);

  return ctx;
}

static void
call_ctx_reset (CallCtx *ctx)
{
  if (ctx->data)
    g_variant_unref (ctx->data);

  g_clear_error (&ctx->error);
}

static void
call_ctx_free (CallCtx *ctx)
{
  g_return_if_fail (ctx != NULL);

  g_main_loop_unref (ctx->loop);
  call_ctx_reset (ctx);
  g_free (ctx);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (CallCtx, call_ctx_free);

static void
dbus_call_done (GObject      *source_object,
                GAsyncResult *res,
                gpointer      user_data)
{
  CallCtx *ctx = user_data;
  GDBusConnection *bus = G_DBUS_CONNECTION (source_object);

  ctx->data = g_dbus_connection_call_finish (bus, res, &ctx->error);
  g_main_loop_quit (ctx->loop);
}

static void
call_ctx_run (CallCtx *ctx)
{
  call_ctx_reset (ctx);
  g_main_loop_run (ctx->loop);
}

/* the actual tests */
static void
test_exported_export (TestExported *unused, gconstpointer data)
{
  g_autoptr(GError) err = NULL;
  g_autoptr(GDBusConnection) bus = NULL;
  g_autoptr(BtExported) obj = NULL;
  g_autofree char *want = NULL;
  g_autofree char *have = NULL;
  const char *obj_path = NULL;
  gboolean exported = FALSE;
  gboolean ok;

  bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &err);

  g_assert_no_error (err);
  g_assert_nonnull (bus);
  g_assert_false (g_dbus_connection_is_closed (bus));

  obj = g_object_new (BT_TYPE_EXPORTED, NULL);
  g_assert_nonnull (obj);

  /* we test auto object path generation */
  want = g_build_path ("/", DBUS_OPATH_BASE, obj->object_id, NULL);
  ok = bolt_exported_export (BOLT_EXPORTED (obj),
                             bus,
                             NULL,
                             &err);

  g_assert_no_error (err);
  g_assert_true (ok);

  obj_path = bolt_exported_get_object_path (BOLT_EXPORTED (obj));

  g_assert_cmpstr (want, ==, obj_path);

  g_object_get (obj,
                "object-path", &have,
                "exported", &exported,
                NULL);

  g_assert_cmpstr (want, ==, have);
  g_assert_true (exported);
  g_assert_true (bolt_exported_is_exported (BOLT_EXPORTED (obj)));

  /* unexport */
  ok = bolt_exported_unexport (BOLT_EXPORTED (obj));

  g_assert_true (ok);
  g_assert_false (bolt_exported_is_exported (BOLT_EXPORTED (obj)));

  /* test we handle special chars in object id */
  bolt_set_strdup (&obj->object_id, "object id-@$1");
  ok = bolt_exported_export (BOLT_EXPORTED (obj),
                             bus,
                             NULL,
                             &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  obj_path = bolt_exported_get_object_path (BOLT_EXPORTED (obj));

  bolt_set_str (&want, g_build_path ("/", DBUS_OPATH_BASE, "object_id___1", NULL));
  g_assert_cmpstr (want, ==, obj_path);
}


static void
test_exported_basic (TestExported *tt, gconstpointer data)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(CallCtx) ctx = NULL;
  GDBusConnection *bus;
  gboolean ok;
  const char *str = NULL;

  ctx = call_ctx_new ();
  bus = tt->bus;

  ok = bolt_exported_export (BOLT_EXPORTED (tt->obj),
                             tt->bus,
                             tt->obj_path,
                             &error);

  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS);
  g_assert_false (ok);
  g_clear_error (&error);

  /* unknown method */
  g_dbus_connection_call (bus,
                          tt->bus_name,
                          tt->obj_path,
                          DBUS_IFACE,
                          "UnknownMethodFooBarSee",
                          NULL,
                          G_VARIANT_TYPE ("(s)"),
                          G_DBUS_CALL_FLAGS_NONE,
                          2000,
                          NULL,
                          dbus_call_done,
                          ctx);
  call_ctx_run (ctx);
  g_assert_error (ctx->error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD);

  /* authorization missing */
  g_dbus_connection_call (bus,
                          tt->bus_name,
                          tt->obj_path,
                          DBUS_IFACE,
                          "Ping",
                          NULL,
                          G_VARIANT_TYPE ("(s)"),
                          G_DBUS_CALL_FLAGS_NONE,
                          2000,
                          NULL,
                          dbus_call_done,
                          ctx);
  call_ctx_run (ctx);
  g_assert_error (ctx->error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED);

  bt_exported_install_method_authorizer (tt->obj);
  tt->obj->authorize_methods = TRUE;

  g_dbus_connection_call (bus,
                          tt->bus_name,
                          tt->obj_path,
                          DBUS_IFACE,
                          "Ping",
                          NULL,
                          G_VARIANT_TYPE ("(s)"),
                          G_DBUS_CALL_FLAGS_NONE,
                          2000,
                          NULL,
                          dbus_call_done,
                          ctx);
  call_ctx_run (ctx);
  g_assert_no_error (ctx->error);

  g_assert_nonnull (ctx->data);
  g_variant_get (ctx->data, "(&s)", &str);

  g_assert_cmpstr (str, ==, "PONG");

  /* check error handling in BoltExported's method dispatching */
  g_dbus_connection_call (bus,
                          tt->bus_name,
                          tt->obj_path,
                          DBUS_IFACE,
                          "Peng",
                          g_variant_new ("(s)", "Out of cheese"),
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          2000,
                          NULL,
                          dbus_call_done,
                          ctx);
  call_ctx_run (ctx);
  g_assert_error (ctx->error, BOLT_ERROR, BOLT_ERROR_FAILED);
}

static void
test_exported_props (TestExported *tt, gconstpointer data)
{
  g_autoptr(CallCtx) ctx = NULL;
  g_autoptr(GVariant) v = NULL;
  const char *str;

  ctx = call_ctx_new ();

  /* unknown property */
  g_dbus_connection_call (tt->bus,
                          tt->bus_name,
                          tt->obj_path,
                          "org.freedesktop.DBus.Properties",
                          "Get",
                          g_variant_new ("(ss)",
                                         DBUS_IFACE,
                                         "UnknownProperty"),
                          G_VARIANT_TYPE ("(v)"),
                          G_DBUS_CALL_FLAGS_NONE,
                          2000,
                          NULL,
                          dbus_call_done,
                          ctx);


  call_ctx_run (ctx);
  g_assert_error (ctx->error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS);

  /*  */
  g_dbus_connection_call (tt->bus,
                          tt->bus_name,
                          tt->obj_path,
                          "org.freedesktop.DBus.Properties",
                          "Get",
                          g_variant_new ("(ss)",
                                         DBUS_IFACE,
                                         "StrFoo"),
                          G_VARIANT_TYPE ("(v)"),
                          G_DBUS_CALL_FLAGS_NONE,
                          2000,
                          NULL,
                          dbus_call_done,
                          ctx);


  call_ctx_run (ctx);
  g_assert_no_error (ctx->error);

  g_assert_nonnull (ctx->data);
  g_variant_get (ctx->data, "(v)", &v);
  str = g_variant_get_string (v, NULL);
  g_assert_cmpstr (str, ==, tt->obj->str);

  /* property setter - read only property */
  g_dbus_connection_call (tt->bus,
                          tt->bus_name,
                          tt->obj_path,
                          "org.freedesktop.DBus.Properties",
                          "Set",
                          g_variant_new ("(ssv)",
                                         DBUS_IFACE,
                                         "StrFoo",
                                         g_variant_new ("s", "se")),
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          2000,
                          NULL,
                          dbus_call_done,
                          ctx);


  call_ctx_run (ctx);
  g_assert_error (ctx->error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS);

  /* property setter - no setter */
  g_dbus_connection_call (tt->bus,
                          tt->bus_name,
                          tt->obj_path,
                          "org.freedesktop.DBus.Properties",
                          "Set",
                          g_variant_new ("(ssv)",
                                         DBUS_IFACE,
                                         "StrRWNoSetter",
                                         g_variant_new ("s", "se")),
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          2000,
                          NULL,
                          dbus_call_done,
                          ctx);


  call_ctx_run (ctx);
  g_assert_error (ctx->error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS);

  /* property setter - not authorized */
  g_dbus_connection_call (tt->bus,
                          tt->bus_name,
                          tt->obj_path,
                          "org.freedesktop.DBus.Properties",
                          "Set",
                          g_variant_new ("(ssv)",
                                         DBUS_IFACE,
                                         "StrRW",
                                         g_variant_new ("s", "se")),
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          2000,
                          NULL,
                          dbus_call_done,
                          ctx);

  call_ctx_run (ctx);
  g_assert_error (ctx->error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED);

  /* install the auth handler, be reject the auth request in it */
  bt_exported_install_property_authorizer (tt->obj);
  tt->obj->authorize_properties = FALSE;

  g_dbus_connection_call (tt->bus,
                          tt->bus_name,
                          tt->obj_path,
                          "org.freedesktop.DBus.Properties",
                          "Set",
                          g_variant_new ("(ssv)",
                                         DBUS_IFACE,
                                         "StrRW",
                                         g_variant_new ("s", "se")),
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          2000,
                          NULL,
                          dbus_call_done,
                          ctx);

  call_ctx_run (ctx);
  g_assert_error (ctx->error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED);

  /* property setter - allow it, but signal an error during property setting */
  tt->obj->authorize_properties = TRUE;
  g_set_error (&tt->obj->setter_err, BOLT_ERROR, BOLT_ERROR_CFG, "failed");

  g_dbus_connection_call (tt->bus,
                          tt->bus_name,
                          tt->obj_path,
                          "org.freedesktop.DBus.Properties",
                          "Set",
                          g_variant_new ("(ssv)",
                                         DBUS_IFACE,
                                         "StrRW",
                                         g_variant_new ("s", "se")),
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          2000,
                          NULL,
                          dbus_call_done,
                          ctx);
  call_ctx_run (ctx);
  g_assert_error (ctx->error, BOLT_ERROR, BOLT_ERROR_CFG);
  g_clear_error (&tt->obj->setter_err);

  /* property setter - allow it, should work now */
  tt->obj->authorize_properties = TRUE;
  str = "new property value";
  g_dbus_connection_call (tt->bus,
                          tt->bus_name,
                          tt->obj_path,
                          "org.freedesktop.DBus.Properties",
                          "Set",
                          g_variant_new ("(ssv)",
                                         DBUS_IFACE,
                                         "StrRW",
                                         g_variant_new ("s", str)),
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          2000,
                          NULL,
                          dbus_call_done,
                          ctx);

  call_ctx_run (ctx);
  g_assert_no_error (ctx->error);

  g_assert_cmpstr (str, ==, tt->obj->str);
}

static void
props_changed_signal (GDBusConnection *connection,
                      const gchar     *sender_name,
                      const gchar     *object_path,
                      const gchar     *interface_name,
                      const gchar     *signal_name,
                      GVariant        *parameters,
                      gpointer         user_data)
{
  CallCtx *ctx = user_data;
  const gchar *interface_name_for_signal;
  GVariant *changed_properties;

  g_auto(GStrv) invalidated_properties = NULL;

  g_variant_get (parameters,
                 "(&s@a{sv}^a&s)",
                 &interface_name_for_signal,
                 &changed_properties,
                 &invalidated_properties);

  g_debug ("got prop changes signal");
  ctx->data = changed_properties; /* transfer ownership */
  if (g_main_loop_is_running (ctx->loop))
    g_main_loop_quit (ctx->loop);
}

static gboolean
change_properties (gpointer user_data)
{
  TestExported *tt = user_data;

  g_object_set (tt->obj,
                "str-rw", "huhu",
                "bool", TRUE,
                NULL);

  return G_SOURCE_REMOVE;
}

static void
test_exported_props_changed (TestExported *tt, gconstpointer data)
{
  g_autoptr(CallCtx) ctx = NULL;
  g_autoptr(GVariantIter) iter = NULL;
  gboolean have_bool = FALSE;
  gboolean have_str = FALSE;
  GVariant *value;
  const char *key;
  guint sid;

  ctx = call_ctx_new ();

  sid = g_dbus_connection_signal_subscribe (tt->bus,
                                            tt->bus_name,
                                            "org.freedesktop.DBus.Properties",
                                            "PropertiesChanged",
                                            tt->obj_path,
                                            DBUS_IFACE,
                                            G_DBUS_SIGNAL_FLAGS_NONE,
                                            props_changed_signal,
                                            ctx,
                                            NULL);

  g_assert_cmpuint (sid, >, 0);

  g_idle_add (change_properties, tt);

  call_ctx_run (ctx);
  g_dbus_connection_signal_unsubscribe (tt->bus, sid);

  g_assert_no_error (ctx->error);
  g_assert_nonnull (ctx->data);

  g_variant_get (ctx->data, "a{sv}", &iter);
  while (g_variant_iter_next (iter, "{&sv}", &key, &value))
    {
      if (bolt_streq (key, "Bool"))
        {
          have_bool = TRUE;
          g_assert_true (g_variant_get_boolean (value) == tt->obj->prop_bool);
        }
      else if (bolt_streq (key, "StrRW"))
        {
          have_str = TRUE;
          g_assert_cmpstr (g_variant_get_string (value, NULL), ==, tt->obj->str);
        }

      g_variant_unref (value);
    }

  g_assert_true (have_bool);
  g_assert_true (have_str);
}

static void
test_exported_props_enums (TestExported *tt, gconstpointer data)
{
  g_autoptr(CallCtx) ctx = NULL;
  g_autoptr(GVariant) v = NULL;
  const char *have;
  const char *want;

  ctx = call_ctx_new ();

  tt->obj->security = BOLT_SECURITY_SECURE;

  /* valid property value, should be converted to string */
  g_dbus_connection_call (tt->bus,
                          tt->bus_name,
                          tt->obj_path,
                          "org.freedesktop.DBus.Properties",
                          "Get",
                          g_variant_new ("(ss)",
                                         DBUS_IFACE,
                                         "Security"),
                          G_VARIANT_TYPE ("(v)"),
                          G_DBUS_CALL_FLAGS_NONE,
                          2000,
                          NULL,
                          dbus_call_done,
                          ctx);

  call_ctx_run (ctx);
  g_assert_no_error (ctx->error);

  g_assert_nonnull (ctx->data);
  g_variant_get (ctx->data, "(v)", &v);
  have = g_variant_get_string (v, NULL);
  want = bolt_security_to_string (tt->obj->security);
  g_assert_cmpstr (have, ==, want);

  /* setter */
  bt_exported_install_property_authorizer (tt->obj);
  tt->obj->authorize_properties = TRUE;
  have = bolt_security_to_string (BOLT_SECURITY_USER);

  g_dbus_connection_call (tt->bus,
                          tt->bus_name,
                          tt->obj_path,
                          "org.freedesktop.DBus.Properties",
                          "Set",
                          g_variant_new ("(ssv)",
                                         DBUS_IFACE,
                                         "Security",
                                         g_variant_new ("s", have)),
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          2000,
                          NULL,
                          dbus_call_done,
                          ctx);

  call_ctx_run (ctx);
  g_assert_no_error (ctx->error);
  g_assert_cmpint (tt->obj->security, ==, BOLT_SECURITY_USER);
}

static void
test_exported_props_flags (TestExported *tt, gconstpointer data)
{
  g_autoptr(CallCtx) ctx = NULL;
  g_autoptr(GVariant) v = NULL;
  g_autofree char *want = NULL;
  g_autofree char *ref = NULL;
  const char *have = NULL;
  BoltKittFlags kf = BOLT_KITT_ENABLED | BOLT_KITT_TURBO_BOOST;

  ctx = call_ctx_new ();

  tt->obj->kitt = BOLT_KITT_DEFAULT;

  /* valid property value, should be converted to string */
  g_dbus_connection_call (tt->bus,
                          tt->bus_name,
                          tt->obj_path,
                          "org.freedesktop.DBus.Properties",
                          "Get",
                          g_variant_new ("(ss)",
                                         DBUS_IFACE,
                                         "KittMode"),
                          G_VARIANT_TYPE ("(v)"),
                          G_DBUS_CALL_FLAGS_NONE,
                          2000,
                          NULL,
                          dbus_call_done,
                          ctx);

  call_ctx_run (ctx);
  g_assert_no_error (ctx->error);

  g_assert_nonnull (ctx->data);
  g_variant_get (ctx->data, "(v)", &v);
  have = g_variant_get_string (v, NULL);
  want = bolt_flags_to_string (BOLT_TYPE_KITT_FLAGS,
                               tt->obj->kitt,
                               NULL);
  g_assert_cmpstr (have, ==, want);

  /* setter */
  bt_exported_install_property_authorizer (tt->obj);
  tt->obj->authorize_properties = TRUE;
  ref = bolt_flags_to_string (BOLT_TYPE_KITT_FLAGS, kf, NULL);

  g_dbus_connection_call (tt->bus,
                          tt->bus_name,
                          tt->obj_path,
                          "org.freedesktop.DBus.Properties",
                          "Set",
                          g_variant_new ("(ssv)",
                                         DBUS_IFACE,
                                         "KittMode",
                                         g_variant_new ("s", ref)),
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          2000,
                          NULL,
                          dbus_call_done,
                          ctx);

  call_ctx_run (ctx);
  g_assert_no_error (ctx->error);
  g_assert_cmpint (tt->obj->kitt, ==, kf);

  /* setter with invalid argument */

  g_dbus_connection_call (tt->bus,
                          tt->bus_name,
                          tt->obj_path,
                          "org.freedesktop.DBus.Properties",
                          "Set",
                          g_variant_new ("(ssv)",
                                         DBUS_IFACE,
                                         "KittMode",
                                         g_variant_new ("s", "invalid | foobar")),
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          2000,
                          NULL,
                          dbus_call_done,
                          ctx);

  call_ctx_run (ctx);
  g_assert_error (ctx->error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS);
}

static void
test_exported_props_object (TestExported *tt, gconstpointer data)
{
  g_autoptr(CallCtx) ctx = NULL;
  g_autoptr(GVariant) v = NULL;
  g_autofree char *want = NULL;
  const char *have;

  ctx = call_ctx_new ();

  /* valid property value, should be converted to string */
  g_dbus_connection_call (tt->bus,
                          tt->bus_name,
                          tt->obj_path,
                          "org.freedesktop.DBus.Properties",
                          "Get",
                          g_variant_new ("(ss)",
                                         DBUS_IFACE,
                                         "Object"),
                          G_VARIANT_TYPE ("(v)"),
                          G_DBUS_CALL_FLAGS_NONE,
                          2000,
                          NULL,
                          dbus_call_done,
                          ctx);

  call_ctx_run (ctx);
  g_assert_no_error (ctx->error);

  g_assert_nonnull (ctx->data);
  g_variant_get (ctx->data, "(v)", &v);
  have = g_variant_get_string (v, NULL);
  g_object_get (tt->obj->prop_obj, "id", &want, NULL);

  g_assert_cmpstr (have, ==, want);

  /* setter, should be an error (invalid arguments) */
  bt_exported_install_property_authorizer (tt->obj);
  tt->obj->authorize_properties = TRUE;
  have = bolt_security_to_string (BOLT_SECURITY_USER);

  g_dbus_connection_call (tt->bus,
                          tt->bus_name,
                          tt->obj_path,
                          "org.freedesktop.DBus.Properties",
                          "Set",
                          g_variant_new ("(ssv)",
                                         DBUS_IFACE,
                                         "Object",
                                         g_variant_new ("s", have)),
                          NULL,
                          G_DBUS_CALL_FLAGS_NONE,
                          2000,
                          NULL,
                          dbus_call_done,
                          ctx);

  call_ctx_run (ctx);
  g_assert_error (ctx->error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS);
}

int
main (int argc, char **argv)
{
  int res;

  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  g_resources_register (bolt_test_get_resource ());

  g_test_add ("/exported/export",
              TestExported,
              NULL,
              NULL,
              test_exported_export,
              NULL);

  g_test_add ("/exported/basic",
              TestExported,
              NULL,
              test_exported_setup,
              test_exported_basic,
              test_exported_teardown);

  g_test_add ("/exported/props",
              TestExported,
              NULL,
              test_exported_setup,
              test_exported_props,
              test_exported_teardown);

  g_test_add ("/exported/props/changed",
              TestExported,
              NULL,
              test_exported_setup,
              test_exported_props_changed,
              test_exported_teardown);

  g_test_add ("/exported/props/enums",
              TestExported,
              NULL,
              test_exported_setup,
              test_exported_props_enums,
              test_exported_teardown);

  g_test_add ("/exported/props/flags",
              TestExported,
              NULL,
              test_exported_setup,
              test_exported_props_flags,
              test_exported_teardown);

  g_test_add ("/exported/props/object",
              TestExported,
              NULL,
              test_exported_setup,
              test_exported_props_object,
              test_exported_teardown);

  test_bus = g_test_dbus_new (G_TEST_DBUS_NONE);
  g_test_dbus_up (test_bus);
  g_assert_nonnull (test_bus);

  g_debug ("test bus at %s\n", g_getenv ("DBUS_SESSION_BUS_ADDRESS"));

  res = g_test_run ();

  g_test_dbus_down (test_bus);
  g_clear_object (&test_bus);

  return res;
}

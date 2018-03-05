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

#include "bolt-client.h"

#include "bolt-device.h"
#include "bolt-error.h"
#include "bolt-names.h"

#include <gio/gio.h>

static void         handle_dbus_device_added (GObject    *self,
                                              GDBusProxy *bus_proxy,
                                              GVariant   *params);
static void         handle_dbus_device_removed (GObject    *self,
                                                GDBusProxy *bus_proxy,
                                                GVariant   *params);

struct _BoltClient
{
  BoltProxy parent;
};

enum {
  PROP_0,

  /* D-Bus Props */
  PROP_VERSION,
  PROP_PROBING,
  PROP_SECURITY,

  PROP_LAST
};

static GParamSpec *props[PROP_LAST] = {NULL, };

enum {
  SIGNAL_DEVICE_ADDED,
  SIGNAL_DEVICE_REMOVED,
  SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = {0};


G_DEFINE_TYPE (BoltClient,
               bolt_client,
               BOLT_TYPE_PROXY);


static void
bolt_client_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  if (bolt_proxy_get_dbus_property (object, pspec, value))
    return;
}

static const BoltProxySignal *
bolt_client_get_dbus_signals (guint *n)
{
  static BoltProxySignal dbus_signals[] = {
    {"DeviceAdded", handle_dbus_device_added},
    {"DeviceRemoved", handle_dbus_device_removed},
  };

  *n = G_N_ELEMENTS (dbus_signals);

  return dbus_signals;
}


static void
bolt_client_class_init (BoltClientClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  BoltProxyClass *proxy_class = BOLT_PROXY_CLASS (klass);

  gobject_class->get_property = bolt_client_get_property;

  proxy_class->get_dbus_signals = bolt_client_get_dbus_signals;

  props[PROP_VERSION]
    = g_param_spec_uint ("version",
                         "Version", NULL,
                         0, G_MAXUINT, 0,
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_NAME);

  props[PROP_PROBING]
    = g_param_spec_boolean ("probing",
                            "Probing", NULL,
                            FALSE,
                            G_PARAM_READABLE |
                            G_PARAM_STATIC_NAME);

  props[PROP_SECURITY]
    = g_param_spec_enum ("security-level",
                         "SecurityLevel", NULL,
                         BOLT_TYPE_SECURITY,
                         BOLT_SECURITY_UNKNOWN,
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_NAME);


  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     props);

  /* signals */
  signals[SIGNAL_DEVICE_ADDED] =
    g_signal_new ("device-added",
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE,
                  1, BOLT_TYPE_DEVICE);

  signals[SIGNAL_DEVICE_REMOVED] =
    g_signal_new ("device-removed",
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE,
                  1, G_TYPE_STRING);
}


static void
bolt_client_init (BoltClient *cli)
{
}

static gint
device_sort (gconstpointer ap,
             gconstpointer bp)
{
  BoltDevice *a = BOLT_DEVICE (*((BoltDevice **) ap));
  BoltDevice *b = BOLT_DEVICE (*((BoltDevice **) bp));
  g_autofree char *pa = NULL;
  g_autofree char *pb = NULL;

  g_object_get (a,
                "syspath", &pa,
                NULL);

  g_object_get (b,
                "syspath", &pb,
                NULL);

  return g_strcmp0 (pa, pb);
}

/* dbus signals */

static void
handle_dbus_device_added (GObject *self, GDBusProxy *bus_proxy, GVariant *params)
{
  g_autoptr(GError) error = NULL;
  BoltClient *cli = BOLT_CLIENT (self);
  GDBusConnection *bus;
  const char *opath = NULL;
  BoltDevice *dev;

  bus = g_dbus_proxy_get_connection (bus_proxy);

  g_variant_get_child (params, 0, "&o", &opath);
  dev = bolt_device_new_for_object_path (bus, opath, &error);
  if (!dev)
    {
      g_warning ("Could not construct device: %s", error->message);
      return;
    }

  g_signal_emit (cli, signals[SIGNAL_DEVICE_ADDED], 0, dev);
  g_object_unref (dev);
}

static void
handle_dbus_device_removed (GObject *self, GDBusProxy *bus_proxy, GVariant *params)
{
  BoltClient *cli = BOLT_CLIENT (self);
  const char *opath = NULL;

  g_variant_get_child (params, 0, "&o", &opath);
  g_signal_emit (cli, signals[SIGNAL_DEVICE_REMOVED], 0, opath);
}

/* public methods */

BoltClient *
bolt_client_new (GError **error)
{
  BoltClient *cli;
  GDBusConnection *bus;

  bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, error);
  if (bus == NULL)
    {
      g_prefix_error (error, "Error connecting to D-Bus: ");
      return FALSE;
    }

  cli = g_initable_new (BOLT_TYPE_CLIENT,
                        NULL, error,
                        "g-flags", G_DBUS_PROXY_FLAGS_NONE,
                        "g-connection", bus,
                        "g-name", BOLT_DBUS_NAME,
                        "g-object-path", BOLT_DBUS_PATH,
                        "g-interface-name", BOLT_DBUS_INTERFACE,
                        NULL);

  g_object_unref (bus);

  return cli;
}

static void
got_the_client (GObject      *source,
                GAsyncResult *res,
                gpointer      user_data)
{
  g_autoptr(GError) error = NULL;
  GTask *task = user_data;
  GObject *obj;

  obj = g_async_initable_new_finish (G_ASYNC_INITABLE (source), res, &error);

  if (obj == NULL)
    {
      g_task_return_error (task, error);
      return;
    }

  g_task_return_pointer (task, obj, g_object_unref);
  g_object_unref (task);
}

static void
got_the_bus (GObject      *source,
             GAsyncResult *res,
             gpointer      user_data)
{
  g_autoptr(GError) error = NULL;
  GTask *task = user_data;
  GCancellable *cancellable;
  GDBusConnection *bus;

  bus = g_bus_get_finish (res, &error);
  if (bus == NULL)
    {
      g_prefix_error (&error, "could not connect to D-Bus: ");
      g_task_return_error (task, error);
      return;
    }

  cancellable = g_task_get_cancellable (task);
  g_async_initable_new_async (BOLT_TYPE_CLIENT,
                              G_PRIORITY_DEFAULT,
                              cancellable,
                              got_the_client, task,
                              "g-flags", G_DBUS_PROXY_FLAGS_NONE,
                              "g-connection", bus,
                              "g-name", BOLT_DBUS_NAME,
                              "g-object-path", BOLT_DBUS_PATH,
                              "g-interface-name", BOLT_DBUS_INTERFACE,
                              NULL);
  g_object_unref (bus);
}

void
bolt_client_new_async (GCancellable       *cancellable,
                       GAsyncReadyCallback callback,
                       gpointer            user_data)
{
  GTask *task;

  task = g_task_new (NULL, cancellable, callback, user_data);
  g_bus_get (G_BUS_TYPE_SYSTEM, cancellable, got_the_bus, task);
}

BoltClient *
bolt_client_new_finish (GAsyncResult *res,
                        GError      **error)
{
  g_return_val_if_fail (G_IS_TASK (res), NULL);

  return g_task_propagate_pointer (G_TASK (res), error);
}

GPtrArray *
bolt_client_list_devices (BoltClient *client,
                          GError    **error)
{
  g_autoptr(GVariant) val = NULL;
  g_autoptr(GPtrArray) devices = NULL;
  g_autoptr(GVariantIter) iter = NULL;
  GDBusConnection *bus = NULL;
  const char *d;

  g_return_val_if_fail (BOLT_IS_CLIENT (client), NULL);

  val = g_dbus_proxy_call_sync (G_DBUS_PROXY (client),
                                "ListDevices",
                                NULL,
                                G_DBUS_CALL_FLAGS_NONE,
                                -1,
                                NULL,
                                error);
  if (val == NULL)
    return NULL;

  bus = g_dbus_proxy_get_connection (G_DBUS_PROXY (client));

  devices = g_ptr_array_new_with_free_func (g_object_unref);

  g_variant_get (val, "(ao)", &iter);
  while (g_variant_iter_loop (iter, "&o", &d, NULL))
    {
      BoltDevice *dev;

      dev = bolt_device_new_for_object_path (bus, d, error);
      if (dev == NULL)
        return NULL;

      g_ptr_array_add (devices, dev);
    }

  g_ptr_array_sort (devices, device_sort);
  return g_steal_pointer (&devices);
}

BoltDevice *
bolt_client_get_device (BoltClient *client,
                        const char *uid,
                        GError    **error)
{
  g_autoptr(GVariant) val = NULL;
  g_autoptr(GError) err = NULL;
  BoltDevice *dev = NULL;
  GDBusConnection *bus = NULL;
  const char *opath = NULL;

  g_return_val_if_fail (BOLT_IS_CLIENT (client), NULL);

  val = g_dbus_proxy_call_sync (G_DBUS_PROXY (client),
                                "DeviceByUid",
                                g_variant_new ("(s)", uid),
                                G_DBUS_CALL_FLAGS_NONE,
                                -1,
                                NULL,
                                &err);

  if (val == NULL)
    {
      if (g_dbus_error_is_remote_error (err))
        g_dbus_error_strip_remote_error (err);

      g_propagate_error (error, g_steal_pointer (&err));
      return NULL;
    }

  bus = g_dbus_proxy_get_connection (G_DBUS_PROXY (client));
  g_variant_get (val, "(&o)", &opath);

  if (opath == NULL)
    return NULL;

  dev = bolt_device_new_for_object_path (bus, opath, error);
  return dev;
}

BoltDevice *
bolt_client_enroll_device (BoltClient   *client,
                           const char   *uid,
                           BoltPolicy    policy,
                           BoltAuthFlags flags,
                           GError      **error)
{
  g_autoptr(GVariant) val = NULL;
  g_autoptr(GError) err = NULL;
  g_autofree char *fstr = NULL;
  BoltDevice *dev = NULL;
  GDBusConnection *bus = NULL;
  GVariant *params = NULL;
  const char *opath = NULL;
  const char *pstr;

  g_return_val_if_fail (BOLT_IS_CLIENT (client), NULL);

  pstr = bolt_enum_to_string (BOLT_TYPE_POLICY, policy, error);
  if (pstr == NULL)
    return NULL;

  fstr = bolt_flags_to_string (BOLT_TYPE_AUTH_FLAGS, flags, error);
  if (fstr == NULL)
    return NULL;

  params = g_variant_new ("(sss)", uid, pstr, fstr);
  val = g_dbus_proxy_call_sync (G_DBUS_PROXY (client),
                                "EnrollDevice",
                                params,
                                G_DBUS_CALL_FLAGS_NONE,
                                -1,
                                NULL,
                                &err);

  if (val == NULL)
    {
      if (g_dbus_error_is_remote_error (err))
        g_dbus_error_strip_remote_error (err);

      g_propagate_error (error, g_steal_pointer (&err));
      return NULL;
    }

  bus = g_dbus_proxy_get_connection (G_DBUS_PROXY (client));
  g_variant_get (val, "(&o)", &opath);

  if (opath == NULL)
    return NULL;

  dev = bolt_device_new_for_object_path (bus, opath, error);
  return dev;
}

gboolean
bolt_client_forget_device (BoltClient *client,
                           const char *uid,
                           GError    **error)
{
  g_autoptr(GVariant) val = NULL;
  g_autoptr(GError) err = NULL;

  g_return_val_if_fail (BOLT_IS_CLIENT (client), FALSE);

  val = g_dbus_proxy_call_sync (G_DBUS_PROXY (client),
                                "ForgetDevice",
                                g_variant_new ("(s)", uid),
                                G_DBUS_CALL_FLAGS_NONE,
                                -1,
                                NULL,
                                &err);

  if (err != NULL)
    {
      if (g_dbus_error_is_remote_error (err))
        g_dbus_error_strip_remote_error (err);

      g_propagate_error (error, g_steal_pointer (&err));
      return FALSE;
    }

  return TRUE;
}

void
bolt_client_forget_device_async (BoltClient         *client,
                                 const char         *uid,
                                 GCancellable       *cancellable,
                                 GAsyncReadyCallback callback,
                                 gpointer            user_data)
{
  g_return_if_fail (BOLT_IS_CLIENT (client));

  g_dbus_proxy_call (G_DBUS_PROXY (client),
                     "ForgetDevice",
                     g_variant_new ("(s)", uid),
                     G_DBUS_CALL_FLAGS_NONE,
                     -1,
                     cancellable,
                     callback,
                     user_data);
}

gboolean
bolt_client_forget_device_finish (BoltClient   *client,
                                  GAsyncResult *res,
                                  GError      **error)
{
  g_autoptr(GVariant) val = NULL;
  g_autoptr(GError) err = NULL;

  g_return_val_if_fail (BOLT_IS_CLIENT (client), FALSE);

  val = g_dbus_proxy_call_finish (G_DBUS_PROXY (client), res, &err);
  if (err != NULL)
    {
      if (g_dbus_error_is_remote_error (err))
        g_dbus_error_strip_remote_error (err);

      g_propagate_error (error, g_steal_pointer (&err));
      return FALSE;
    }

  return TRUE;
}

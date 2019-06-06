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

#include "bolt-error.h"
#include "bolt-power.h"

#include <gio/gio.h>
#include <gio/gunixfdlist.h>

struct _BoltPower
{
  BoltProxy parent;
};

enum {
  PROP_0,

  /* D-Bus Props */
  PROP_SUPPORTED,
  PROP_STATE,
  PROP_TIMEOUT,

  PROP_LAST
};

static GParamSpec *props[PROP_LAST] = {NULL, };

G_DEFINE_TYPE (BoltPower,
               bolt_power,
               BOLT_TYPE_PROXY);


static void
bolt_power_class_init (BoltPowerClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = bolt_proxy_property_getter;
  gobject_class->set_property = bolt_proxy_property_setter;

  props[PROP_SUPPORTED] =
    g_param_spec_boolean ("supported", "Supported",
                          "Is forcing power supported?",
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS);

  props[PROP_STATE] =
    g_param_spec_enum ("state", "State",
                       "Current force power state.",
                       BOLT_TYPE_POWER_STATE,
                       BOLT_FORCE_POWER_UNSET,
                       G_PARAM_READABLE |
                       G_PARAM_STATIC_STRINGS);

  props[PROP_TIMEOUT] =
    g_param_spec_uint ("timeout", "Timeout",
                       "Force power timeout.",
                       0, G_MAXINT, 0,
                       G_PARAM_READABLE |
                       G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     props);

}

static void
bolt_power_init (BoltPower *power)
{
}

/* public methods */
BoltPower *
bolt_power_new_for_object_path (GDBusConnection *bus,
                                GCancellable    *cancel,
                                GError         **error)
{
  BoltPower *pwr;

  g_return_val_if_fail (G_IS_DBUS_CONNECTION (bus), NULL);
  g_return_val_if_fail (!cancel || G_IS_CANCELLABLE (cancel), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  pwr = g_initable_new (BOLT_TYPE_POWER,
                        cancel, error,
                        "g-flags", G_DBUS_PROXY_FLAGS_NONE,
                        "g-connection", bus,
                        "g-name", BOLT_DBUS_NAME,
                        "g-object-path", BOLT_DBUS_PATH,
                        "g-interface-name", BOLT_DBUS_POWER_INTERFACE,
                        NULL);

  return pwr;
}


int
bolt_power_force_power (BoltPower *power,
                        GError   **error)
{
  g_autoptr(GUnixFDList) fds = NULL;
  g_autoptr(GVariant) val = NULL;
  g_autoptr(GError) err = NULL;
  GVariant *input;
  int fd = -1;

  g_return_val_if_fail (BOLT_IS_POWER (power), -1);
  g_return_val_if_fail (error == NULL || *error == NULL, -1);

  input = g_variant_new ("(ss)",
                         "boltctl", /* who */
                         "");       /* flags */

  val = g_dbus_proxy_call_with_unix_fd_list_sync (G_DBUS_PROXY (power),
                                                  "ForcePower",
                                                  input,
                                                  G_DBUS_CALL_FLAGS_NONE,
                                                  -1,
                                                  NULL,
                                                  &fds,
                                                  NULL,
                                                  &err);

  if (val == NULL)
    {
      bolt_error_propagate_stripped (error, &err);
      return -1;
    }

  if (g_unix_fd_list_get_length (fds) != 1)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "invalid number of file descriptors returned: %d",
                   g_unix_fd_list_get_length (fds));

      return -1;
    }

  fd = g_unix_fd_list_get (fds, 0, NULL);

  return fd;
}

GPtrArray *
bolt_power_list_guards (BoltPower    *power,
                        GCancellable *cancel,
                        GError      **error)
{
  g_autoptr(GVariant) val = NULL;
  g_autoptr(GVariantIter) iter = NULL;
  GPtrArray *res;
  const char *id;
  const char *who;
  guint pid;

  g_return_val_if_fail (BOLT_IS_POWER (power), NULL);
  g_return_val_if_fail (!cancel || G_IS_CANCELLABLE (cancel), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  val = g_dbus_proxy_call_sync (G_DBUS_PROXY (power),
                                "ListGuards",
                                NULL,
                                G_DBUS_CALL_FLAGS_NONE,
                                -1,
                                cancel,
                                error);
  if (val == NULL)
    return NULL;

  res = g_ptr_array_new_with_free_func ((GDestroyNotify) bolt_power_guard_free);
  g_variant_get (val, "(a(ssu))", &iter);
  while (g_variant_iter_loop (iter, "(ssu)", &id, &who, &pid))
    {
      BoltPowerGuard *g = g_new (BoltPowerGuard, 1);

      g->id = g_strdup (id);
      g->who = g_strdup (who);
      g->pid = pid;

      g_ptr_array_add (res, g);
    }

  return res;
}

/* getter */
gboolean
bolt_power_is_supported (BoltPower *power)
{
  gboolean val;

  g_return_val_if_fail (BOLT_IS_POWER (power), FALSE);

  val = bolt_proxy_get_bool_by_pspec (power, props[PROP_SUPPORTED]);

  return val;
}

BoltPowerState
bolt_power_get_state (BoltPower *power)
{
  gint val = BOLT_FORCE_POWER_UNSET;

  g_return_val_if_fail (BOLT_IS_POWER (power), val);

  val = bolt_proxy_get_enum_by_pspec (power, props[PROP_STATE]);

  return val;
}

/* bolt power guard functions */
void
bolt_power_guard_free (BoltPowerGuard *guard)
{
  g_return_if_fail (guard != NULL);

  g_free (guard->id);
  g_free (guard->who);
  g_free (guard);
}

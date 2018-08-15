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

#include "config.h"

#include "bolt-power.h"

#include "bolt-enums.h"
#include "bolt-fs.h"
#include "bolt-log.h"
#include "bolt-io.h"
#include "bolt-str.h"

#include <libudev.h>

#define POWER_WAIT_TIMEOUT 20 * 1000 // 20 seconds
#define DEFAULT_RUNDIR "/run/boltd/"
#define DEFAULT_STATEDIR "power"

typedef struct udev_device udev_device;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (udev_device, udev_device_unref);

static void       bolt_power_release (BoltPower      *power,
                                      BoltPowerGuard *guard);

/* BoltPowerGuard  */

struct _BoltPowerGuard
{
  GObject object;

  /* book-keeping */
  BoltPower *power;

  /* properties */
  char *id;
  char *who;
};

enum {
  PROP_GUARD_0,

  PROP_POWER,

  PROP_ID,
  PROP_WHO,

  PROP_GUARD_LAST
};

static GParamSpec *guard_props[PROP_GUARD_LAST] = { NULL, };

G_DEFINE_TYPE (BoltPowerGuard,
               bolt_power_guard,
               G_TYPE_OBJECT);

static void
bolt_power_guard_finalize (GObject *object)
{
  BoltPowerGuard *guard = BOLT_POWER_GUARD (object);

  /* release the lock we have to force power,
  * object must be intact for this to work */
  bolt_power_release (guard->power, guard);

  g_clear_pointer (&guard->who, g_free);
  g_clear_pointer (&guard->id, g_free);
  g_clear_object (&guard->power);

  G_OBJECT_CLASS (bolt_power_guard_parent_class)->finalize (object);
}

static void
bolt_power_guard_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  BoltPowerGuard *guard = BOLT_POWER_GUARD (object);

  switch (prop_id)
    {
    case PROP_POWER:
      g_value_set_object (value, guard->power);
      break;

    case PROP_ID:
      g_value_set_string (value, guard->id);
      break;

    case PROP_WHO:
      g_value_set_string (value, guard->who);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}


static void
bolt_power_guard_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  BoltPowerGuard *guard = BOLT_POWER_GUARD (object);

  switch (prop_id)
    {
    case PROP_POWER:
      guard->power = g_value_dup_object (value);
      break;

    case PROP_ID:
      guard->id = g_value_dup_string (value);
      break;

    case PROP_WHO:
      guard->who = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}


static void
bolt_power_guard_init (BoltPowerGuard *power)
{
}

static void
bolt_power_guard_class_init (BoltPowerGuardClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = bolt_power_guard_finalize;
  gobject_class->get_property = bolt_power_guard_get_property;
  gobject_class->set_property = bolt_power_guard_set_property;

  guard_props[PROP_POWER] =
    g_param_spec_object ("power",
                         NULL, NULL,
                         BOLT_TYPE_POWER,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  guard_props[PROP_ID] =
    g_param_spec_string ("id",
                         NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  guard_props[PROP_WHO] =
    g_param_spec_string ("who",
                         NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class,
                                     PROP_GUARD_LAST,
                                     guard_props);
}

/* ****************************************************************** */
/* BoltPower */

static void     power_initable_iface_init (GInitableIface *iface);

static gboolean bolt_power_initialize (GInitable    *initable,
                                       GCancellable *cancellable,
                                       GError      **error);

static void     handle_uevent_udev (BoltUdev           *udev,
                                    const char         *action,
                                    struct udev_device *device,
                                    gpointer            user_data);

static gboolean bolt_power_switch_toggle (BoltPower *power,
                                          gboolean   on,
                                          GError   **error);

struct _BoltPower
{
  GObject object;

  /* path to store run time data  */
  char  *runpath;
  GFile *statedir;

  /* connection to udev */
  BoltUdev *udev;

  /* the path to the sysfs device file,
   * or NULL if force power is unavailable */
  char          *path;
  BoltPowerState state;

  /*  */
  guint16     guard_num;
  GHashTable *guards;

  /* wait before off handling */
  guint wait_id;
  guint timeout; /* milliseconds */
};

enum {
  PROP_0,

  PROP_RUNDIR,
  PROP_STATEDIR,
  PROP_UDEV,
  PROP_SUPPORTED,
  PROP_STATE,
  PROP_TIMEOUT,

  PROP_LAST
};

static GParamSpec *power_props[PROP_LAST] = { NULL, };

G_DEFINE_TYPE_WITH_CODE (BoltPower,
                         bolt_power,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                power_initable_iface_init));

static void
bolt_power_finalize (GObject *object)
{
  BoltPower *power = BOLT_POWER (object);

  if (power->wait_id != 0)
    g_source_remove (power->wait_id);

  g_clear_pointer (&power->runpath, g_free);
  g_clear_object (&power->statedir);
  g_clear_object (&power->udev);
  g_clear_pointer (&power->path, g_free);
  g_clear_pointer (&power->guards, g_hash_table_unref);

  G_OBJECT_CLASS (bolt_power_parent_class)->finalize (object);
}


static void
bolt_power_init (BoltPower *power)
{
  power->state = BOLT_FORCE_POWER_UNSET;
  power->guards = g_hash_table_new (g_str_hash, g_str_equal);
}

static void
bolt_power_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  BoltPower *power = BOLT_POWER (object);

  switch (prop_id)
    {

    case PROP_RUNDIR:
      g_value_set_string (value, power->runpath);
      break;

    case PROP_STATEDIR:
      g_value_set_object (value, power->statedir);
      break;

    case PROP_UDEV:
      g_value_set_object (value, power->udev);
      break;

    case PROP_SUPPORTED:
      g_value_set_boolean (value, power->path != NULL);
      break;

    case PROP_STATE:
      g_value_set_enum (value, power->state);
      break;

    case PROP_TIMEOUT:
      g_value_set_uint (value, power->timeout);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bolt_power_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  BoltPower *power = BOLT_POWER (object);

  switch (prop_id)
    {
    case PROP_RUNDIR:
      power->runpath = g_value_dup_string (value);
      break;

    case PROP_UDEV:
      power->udev = g_value_dup_object (value);
      break;

    case PROP_TIMEOUT:
      power->timeout = g_value_get_uint (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}


static void
bolt_power_class_init (BoltPowerClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = bolt_power_finalize;

  gobject_class->get_property = bolt_power_get_property;
  gobject_class->set_property = bolt_power_set_property;

  power_props[PROP_RUNDIR] =
    g_param_spec_string ("rundir",
                         NULL, NULL,
                         DEFAULT_RUNDIR,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  power_props[PROP_STATEDIR] =
    g_param_spec_string ("statedir",
                         NULL, NULL,
                         DEFAULT_RUNDIR,
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_STRINGS);

  power_props[PROP_UDEV] =
    g_param_spec_object ("udev",
                         NULL, NULL,
                         BOLT_TYPE_UDEV,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  power_props[PROP_SUPPORTED] =
    g_param_spec_boolean ("supported",
                          NULL, NULL,
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_NICK);

  power_props[PROP_STATE] =
    g_param_spec_enum ("state",
                       NULL, NULL,
                       BOLT_TYPE_POWER_STATE,
                       BOLT_FORCE_POWER_UNSET,
                       G_PARAM_READABLE |
                       G_PARAM_STATIC_STRINGS);

  power_props[PROP_TIMEOUT] =
    g_param_spec_uint ("timeout",
                       NULL, NULL,
                       0, G_MAXINT, POWER_WAIT_TIMEOUT,
                       G_PARAM_READWRITE |
                       G_PARAM_CONSTRUCT_ONLY |
                       G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     power_props);
}

static void
power_initable_iface_init (GInitableIface *iface)
{
  iface->init = bolt_power_initialize;
}

static gboolean
bolt_power_initialize (GInitable    *initable,
                       GCancellable *cancellable,
                       GError      **error)
{
  g_autoptr(GError) err = NULL;
  g_autofree char *statedir = NULL;
  BoltPower *power = BOLT_POWER (initable);
  struct udev_enumerate *e;
  struct udev_list_entry *l, *devices;
  gboolean ok;

  statedir = g_build_filename (power->runpath, DEFAULT_STATEDIR, NULL);
  power->statedir = g_file_new_for_path (statedir);

  ok = g_file_make_directory_with_parents (power->statedir, NULL, &err);
  if (!ok && !bolt_err_exists (err))
    bolt_warn_err (err, LOG_TOPIC ("power"),
                   "failed to create guarddir at %s", statedir);
  g_clear_error (&err);

  e = bolt_udev_new_enumerate (power->udev, NULL);
  udev_enumerate_add_match_subsystem (e, "wmi");
  udev_enumerate_add_match_property (e, "DRIVER", "intel-wmi-thunderbolt");

  udev_enumerate_scan_devices (e);
  devices = udev_enumerate_get_list_entry (e);

  udev_list_entry_foreach (l, devices)
    {
      g_autofree char *path = NULL;
      const char *syspath;

      syspath = udev_list_entry_get_name (l);
      path = g_build_filename (syspath, "force_power", NULL);

      if (g_file_test (path, G_FILE_TEST_IS_REGULAR))
        {
          power->path = g_steal_pointer (&path);
          break;
        }
    }

  udev_enumerate_unref (e);

  g_signal_connect_object (power->udev, "uevent",
                           (GCallback) handle_uevent_udev,
                           power, 0);

  return TRUE;
}

/* internal methods */
static gboolean
bolt_power_wait_timeout (gpointer user_data)
{
  g_autoptr(GError) err = NULL;
  BoltPower *power = user_data;
  gboolean ok;

  /* we just removed the last active guard */
  ok = bolt_power_switch_toggle (power, FALSE, &err);

  if (!ok)
    bolt_warn_err (err, LOG_TOPIC ("power"),
                   "failed to turn off force_power");
  else
    bolt_info (LOG_TOPIC ("power"), "setting force_power to OFF");

  power->wait_id = 0;
  return G_SOURCE_REMOVE;
}

static void
handle_uevent_udev (BoltUdev           *udev,
                    const char         *action,
                    struct udev_device *device,
                    gpointer            user_data)
{
  BoltPower *power = BOLT_POWER (user_data);
  const char *subsystem;

  /* no callback scheduled, nothing to do */
  if (power->wait_id == 0)
    return;

  /* only interested in added devices */
  if (!bolt_streq (action, "add"))
    return;

  subsystem = udev_device_get_subsystem (device);
  if (!bolt_streq (subsystem, "thunderbolt"))
    return;

  /* if we are not in WAIT state, we don't
   * do anything, but if we are, we want
   * to reset the timeout */
  if (power->state != BOLT_FORCE_POWER_WAIT)
    return;

  bolt_info (LOG_TOPIC ("power"), "resetting timeout (uevent %s)",
             udev_device_get_syspath (device));

  g_source_remove (power->wait_id);
  power->wait_id = g_timeout_add (power->timeout,
                                  bolt_power_wait_timeout,
                                  power);
}

static gboolean
bolt_power_switch_toggle (BoltPower *power,
                          gboolean   on,
                          GError   **error)
{
  gboolean ok;
  int fd;

  g_return_val_if_fail (BOLT_IS_POWER (power), FALSE);

  if (power->path == NULL)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                           "force power not supported");
      return FALSE;
    }

  fd = bolt_open (power->path, O_WRONLY, 0, error);
  if (fd < 0)
    return FALSE;

  ok = bolt_write_all (fd, on ? "1" : "0", 1, error);
  bolt_close (fd, NULL);

  if (ok)
    {
      power->state = on ? BOLT_FORCE_POWER_ON : BOLT_FORCE_POWER_OFF;
      g_object_notify_by_pspec (G_OBJECT (power),
                                power_props[PROP_STATE]);
    }

  return ok;
}

static char *
bolt_power_gen_guard_id (BoltPower *power,
                         GError   **error)
{
  char *id = NULL;

  if (g_hash_table_size (power->guards) >= G_MAXUINT16)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                           "maximum number of force power locks reached");
      return NULL;
    }

  do
    {
      guint16 i = ++power->guard_num;

      g_free (id);
      id = g_strdup_printf ("%" G_GUINT16_FORMAT, i);

    }
  while (g_hash_table_contains (power->guards, id));

  return id;
}

static void
bolt_power_release (BoltPower *power, BoltPowerGuard *guard)
{
  gboolean ok;

  ok = g_hash_table_remove (power->guards, guard->id);

  if (!ok)
    {
      bolt_bug ("inactive guard ('%s', '%s') found",
                guard->id, guard->who);
      return;
    }

  bolt_info (LOG_TOPIC ("power"), "guard '%s' for '%s' deactivated",
             guard->id, guard->who);

  /* we still have active guards */
  if (g_hash_table_size (power->guards) != 0)
    return;

  /* go into WAIT (from ON) state */
  if (power->wait_id != 0)
    {
      bolt_bug ("have active waiter already");
      return;
    }

  if (power->timeout == 0)
    {
      bolt_info (LOG_TOPIC ("power"), "wait timeout is zero, skipping");
      bolt_power_wait_timeout ((gpointer) power);
      return;
    }

  bolt_info (LOG_TOPIC ("power"), "shutdown scheduled (T-%3.2fs)",
             power->timeout / 1000.0);

  power->wait_id = g_timeout_add (power->timeout,
                                  bolt_power_wait_timeout,
                                  power);

  power->state = BOLT_FORCE_POWER_WAIT;
  g_object_notify_by_pspec (G_OBJECT (power), power_props[PROP_STATE]);
}

/* public methods */
BoltPower *
bolt_power_new (BoltUdev *udev)
{
  BoltPower *power;

  power = g_initable_new (BOLT_TYPE_POWER,
                          NULL, NULL,
                          "udev", udev,
                          NULL);

  return power;
}

GFile *
bolt_power_get_statedir (BoltPower *power)
{
  g_return_val_if_fail (BOLT_IS_POWER (power), FALSE);

  return power->statedir;
}

gboolean
bolt_power_can_force (BoltPower *power)
{
  g_return_val_if_fail (BOLT_IS_POWER (power), FALSE);

  return power->path != NULL;
}

BoltPowerState
bolt_power_get_state (BoltPower *power)
{
  g_return_val_if_fail (BOLT_IS_POWER (power), -1);

  return power->state;
}

BoltPowerGuard *
bolt_power_acquire (BoltPower *power,
                    GError   **error)
{
  g_autofree char *id = NULL;
  BoltPowerGuard *guard;
  gboolean ok;

  g_return_val_if_fail (BOLT_IS_POWER (power), NULL);

  id = bolt_power_gen_guard_id (power, error);

  if (id == NULL)
    return NULL;

  if (power->state == BOLT_FORCE_POWER_WAIT)
    {
      g_source_remove (power->wait_id);
      power->wait_id = 0;
      power->state = BOLT_FORCE_POWER_ON;
      g_object_notify_by_pspec (G_OBJECT (power),
                                power_props[PROP_STATE]);
    }
  else if (power->state != BOLT_FORCE_POWER_ON)
    {
      ok = bolt_power_switch_toggle (power, TRUE, error);
      if (!ok)
        return NULL;
    }

  guard = g_object_new (BOLT_TYPE_POWER_GUARD,
                        "power", power,
                        "id", id,
                        "who", "boltd",
                        NULL);

  /* NB: we don't take a ref here, because we want the
   * guard to act as RAII guard, i.e. when the client
   * releases the last reference to the guard, we call
   * the _release() function in the finalizer */
  g_hash_table_insert (power->guards, guard->id, guard);

  bolt_info (LOG_TOPIC ("power"), "guard '%s' for '%s' active",
             guard->id, guard->who);

  return guard;
}

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

#include "bolt-config.h"
#include "bolt-enums.h"
#include "bolt-error.h"
#include "bolt-fs.h"
#include "bolt-log.h"
#include "bolt-io.h"
#include "bolt-str.h"
#include "bolt-unix.h"

#include <gio/gunixfdlist.h>

#include <libudev.h>
#include <unistd.h>
#include <sys/types.h>

#define POWER_WAIT_TIMEOUT 20 * 1000 // 20 seconds
#define POWER_REAPER_TIMEOUT 20 // seconds
#define DEFAULT_STATEDIR "power"
#define STATE_FILENAME "on"

typedef enum GuardState {
  GUARD_STATE_ACTIVE = 0,
  GUARD_STATE_RELEASED = 1
} GuardState;

typedef struct udev_device udev_device;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (udev_device, udev_device_unref);

static void       bolt_power_release (BoltPower *power,
                                      BoltGuard *guard);

/* BoltGuard  */
static void       bolt_guard_remove (BoltGuard *guard);

struct _BoltGuard
{
  GObject object;

  /* book-keeping */
  GuardState state;
  char      *path;

  char      *fifo;
  guint      watch;

  /* properties */
  char *id;
  char *who;
  pid_t pid;
};

enum {
  PROP_GUARD_0,

  PROP_PATH,
  PROP_FIFO,

  PROP_ID,
  PROP_WHO,
  PROP_PID,


  PROP_GUARD_LAST
};

static GParamSpec *guard_props[PROP_GUARD_LAST] = { NULL, };

enum {
  GUARD_SIGNAL_RELEASED,
  SIGNAL_LAST
};

static guint guard_signals[SIGNAL_LAST] = {0};

G_DEFINE_TYPE (BoltGuard,
               bolt_guard,
               G_TYPE_OBJECT);

static void
bolt_guard_dispose (GObject *object)
{
  BoltGuard *guard = BOLT_GUARD (object);

  /* remove our state file */
  bolt_guard_remove (guard);

  if (guard->state != GUARD_STATE_RELEASED)
    {
      /* signal to clients to that we have been released.
       * NB: we must be intact for method call */
      guard->state = GUARD_STATE_RELEASED;
      g_signal_emit (object, guard_signals[GUARD_SIGNAL_RELEASED], 0);
    }

  G_OBJECT_CLASS (bolt_guard_parent_class)->dispose (object);
}

static void
bolt_guard_finalize (GObject *object)
{
  BoltGuard *guard = BOLT_GUARD (object);

  if (guard->watch)
    g_source_remove (guard->watch);

  g_clear_pointer (&guard->fifo, g_free);
  g_clear_pointer (&guard->who, g_free);
  g_clear_pointer (&guard->id, g_free);

  G_OBJECT_CLASS (bolt_guard_parent_class)->finalize (object);
}

static void
bolt_guard_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  BoltGuard *guard = BOLT_GUARD (object);

  switch (prop_id)
    {
    case PROP_PATH:
      g_value_set_string (value, guard->path);
      break;

    case PROP_FIFO:
      g_value_set_string (value, guard->fifo);
      break;

    case PROP_ID:
      g_value_set_string (value, guard->id);
      break;

    case PROP_WHO:
      g_value_set_string (value, guard->who);
      break;

    case PROP_PID:
      g_value_set_ulong (value, guard->pid);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}


static void
bolt_guard_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  BoltGuard *guard = BOLT_GUARD (object);

  switch (prop_id)
    {
    case PROP_PATH:
      guard->path = g_value_dup_string (value);
      break;

    case PROP_FIFO:
      guard->fifo = g_value_dup_string (value);
      break;

    case PROP_ID:
      guard->id = g_value_dup_string (value);
      break;

    case PROP_WHO:
      guard->who = g_value_dup_string (value);
      break;

    case PROP_PID:
      guard->pid = g_value_get_ulong (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}


static void
bolt_guard_init (BoltGuard *guard)
{
  guard->state = GUARD_STATE_ACTIVE;
}

static void
bolt_guard_class_init (BoltGuardClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->dispose = bolt_guard_dispose;
  gobject_class->finalize = bolt_guard_finalize;
  gobject_class->get_property = bolt_guard_get_property;
  gobject_class->set_property = bolt_guard_set_property;

  guard_props[PROP_PATH] =
    g_param_spec_string ("path",
                         NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  guard_props[PROP_FIFO] =
    g_param_spec_string ("fifo",
                         NULL, NULL,
                         NULL,
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

  guard_props[PROP_PID] =
    g_param_spec_ulong ("pid",
                        NULL, NULL,
                        0, G_MAXULONG, 0,
                        G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class,
                                     PROP_GUARD_LAST,
                                     guard_props);

  guard_signals[GUARD_SIGNAL_RELEASED] =
    g_signal_new ("released",
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE,
                  0);
}

static gboolean
bolt_guard_save (BoltGuard *guard,
                 GFile     *guarddir,
                 GError   **error)
{
  g_autoptr(GFile) guardfile = NULL;
  g_autoptr(GKeyFile) kf = NULL;
  g_autofree char *path = NULL;
  g_autofree char *name = NULL;
  gboolean ok;

  name = g_strdup_printf ("%s.guard", guard->id);
  guardfile = g_file_get_child (guarddir, name);
  path = g_file_get_path (guardfile);

  kf = g_key_file_new ();

  g_key_file_set_string (kf, "guard", "id", guard->id);
  g_key_file_set_string (kf, "guard", "who", guard->who);
  g_key_file_set_uint64 (kf, "guard", "pid", guard->pid);

  ok = g_key_file_save_to_file (kf, path, error);

  if (ok)
    {
      guard->path = g_steal_pointer (&path);
      g_object_notify_by_pspec (G_OBJECT (guard),
                                guard_props[PROP_PATH]);
    }

  return ok;
}

static BoltGuard *
bolt_guard_load (const char *statedir,
                 const char *name,
                 GError    **error)
{
  g_autoptr(GError) err = NULL;
  g_autoptr(GKeyFile) kf = NULL;
  g_autofree char *path = NULL;
  g_autofree char *who = NULL;
  g_autofree char *id = NULL;
  g_autofree char *fifo = NULL;
  gboolean ok;
  gulong pid;

  path = g_build_filename (statedir, name, NULL);

  kf = g_key_file_new ();
  ok = g_key_file_load_from_file (kf, path, 0, error);

  if (!ok)
    return NULL;

  id = g_key_file_get_string (kf, "guard", "id", &err);

  if (id == NULL)
    {
      g_set_error (error, BOLT_ERROR, BOLT_ERROR_FAILED,
                   "could not read 'id' field: %s", err->message);
      return NULL;
    }

  who = g_key_file_get_string (kf, "guard", "who", &err);
  if (who == NULL)
    {
      g_set_error_literal (error, BOLT_ERROR, BOLT_ERROR_FAILED,
                           "field missing ('who')");
      return NULL;
    }

  pid = (gulong) g_key_file_get_uint64 (kf, "guard", "pid", &err);
  if (err != NULL)
    {
      g_set_error_literal (error, BOLT_ERROR, BOLT_ERROR_FAILED,
                           "field missing ('pid')");
      return NULL;
    }

  fifo = g_strdup_printf ("%s.fifo", path);

  if (!g_file_test (fifo, G_FILE_TEST_EXISTS))
    g_clear_pointer (&fifo, g_free);

  return g_object_new (BOLT_TYPE_GUARD,
                       "path", path,
                       "fifo", fifo,
                       "id", id,
                       "who", who,
                       "pid", pid,
                       NULL);
}

static void
bolt_guard_remove (BoltGuard *guard)
{
  g_autoptr(GError) err = NULL;
  gboolean ok;

  /* we are not saved */
  if (guard->path == NULL)
    return;

  /*  */
  if (guard->fifo != NULL)
    {
      bolt_debug (LOG_TOPIC ("power"),
                  "not removing guard '%s' with active fifo",
                  guard->id);
      return;
    }

  ok = bolt_unlink (guard->path, &err);
  if (!ok)
    {
      bolt_warn_err (err, LOG_TOPIC ("power"),
                     "Could not remove power guard: '%s' @ %s",
                     guard->id, guard->path);
      return;
    }

  g_clear_pointer (&guard->path, g_free);
  g_object_notify_by_pspec (G_OBJECT (guard),
                            guard_props[PROP_PATH]);
}

static void
bolt_guard_fifo_cleanup (BoltGuard *guard)
{
  g_autoptr(GError) err = NULL;
  gboolean ok;

  if (guard->fifo == NULL)
    return;

  ok = bolt_unlink (guard->fifo, &err);
  if (!ok)
    {
      bolt_warn_err (err, LOG_TOPIC ("power"),
                     "Could not remove FIFO for power guard: '%s' @ %s",
                     guard->id, guard->fifo);
    }

  g_clear_pointer (&guard->fifo, g_free);
  g_object_notify_by_pspec (G_OBJECT (guard), guard_props[PROP_FIFO]);
}

static gboolean
bolt_guard_mkfifo (BoltGuard *guard,
                   GError   **error)
{
  g_autoptr(GError) err = NULL;
  int r;

  g_return_val_if_fail (BOLT_IS_GUARD (guard), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (guard->fifo == NULL)
    guard->fifo = g_strdup_printf ("%s.fifo", guard->path);

  r = bolt_mkfifo (guard->fifo, 0600, &err);
  if (r == -1 && !bolt_err_exists (err))
    return bolt_error_propagate (error, &err);

  g_object_notify_by_pspec (G_OBJECT (guard),
                            guard_props[PROP_FIFO]);

  return TRUE;
}

static gboolean
power_guard_has_event (GIOChannel  *source,
                       GIOCondition condition,
                       gpointer     data)
{
  BoltGuard *guard = data;

  bolt_info (LOG_TOPIC ("power"), "got event for guard '%s' (%x)",
             guard->id, (guint) condition);
  guard->watch = 0;
  return FALSE;
}

static void
guard_watch_release (gpointer data)
{
  BoltGuard *guard = data;

  if (guard->fifo == NULL)
    {
      bolt_bug (LOG_TOPIC ("power"), "FIFO event but no FIFO");
      return;
    }

  bolt_guard_fifo_cleanup (guard);

  bolt_debug (LOG_TOPIC ("power"),
              "released watch reference for guard '%s'",
              guard->id);

  g_object_unref (guard);
}

int
bolt_guard_monitor (BoltGuard *guard,
                    GError   **error)
{
  g_autoptr(GIOChannel) ch = NULL;
  gboolean ok;
  int fd;

  g_return_val_if_fail (BOLT_IS_GUARD (guard), -1);
  g_return_val_if_fail (error == NULL || *error == NULL, -1);

  ok = bolt_guard_mkfifo (guard, error);
  if (!ok)
    return -1;

  /* reader */
  fd = bolt_open (guard->fifo, O_RDONLY | O_CLOEXEC | O_NONBLOCK, 0, error);
  if (fd == -1)
    return -1;

  ch = g_io_channel_unix_new (fd);

  g_io_channel_set_close_on_unref (ch, TRUE);
  g_io_channel_set_encoding (ch, NULL, NULL);
  g_io_channel_set_buffered (ch, FALSE);
  g_io_channel_set_flags (ch, G_IO_FLAG_NONBLOCK, NULL);
  fd = -1; /* the GIOChannel owns the fd, via _close_on_unref () */
  (void) fd; /* The above is an intentional dead store */

  /* writer */
  fd = bolt_open (guard->fifo, O_WRONLY | O_CLOEXEC | O_NONBLOCK, 0, error);
  if (fd == -1)
    return -1;

  /* NB: we take a ref to the guard here */
  guard->watch = g_io_add_watch_full (ch,
                                      G_PRIORITY_DEFAULT,
                                      G_IO_HUP | G_IO_ERR,
                                      power_guard_has_event,
                                      g_object_ref (guard),
                                      guard_watch_release);

  return fd;
}

const char *
bolt_guard_get_id (BoltGuard *guard)
{
  g_return_val_if_fail (BOLT_IS_GUARD (guard), NULL);

  return guard->id;
}

const char *
bolt_guard_get_who (BoltGuard *guard)
{
  g_return_val_if_fail (BOLT_IS_GUARD (guard), NULL);

  return guard->who;
}

guint
bolt_guard_get_pid (BoltGuard *guard)
{
  g_return_val_if_fail (BOLT_IS_GUARD (guard), 0);

  return (guint) guard->pid;
}

/* ****************************************************************** */
/* BoltPower */

/* gobject */
static void      power_initable_iface_init (GInitableIface *iface);

static gboolean  bolt_power_initialize (GInitable    *initable,
                                        GCancellable *cancellable,
                                        GError      **error);
/* utilities */
static char *    bolt_power_gen_guard_id (BoltPower *power,
                                          GError   **error);

static gboolean  bolt_power_recover_guards (BoltPower *power,
                                            GError   **error);

static void      bolt_power_timeout_reset (BoltPower *power);

static gboolean  bolt_power_switch_toggle (BoltPower *power,
                                           gboolean   on,
                                           GError   **error);

/* callbacks and signals */
static gboolean bolt_power_wait_timeout (gpointer user_data);

static gboolean bolt_power_reaper_timeout (gpointer user_data);


static void     handle_uevent_udev (BoltUdev           *udev,
                                    const char         *action,
                                    struct udev_device *device,
                                    gpointer            user_data);

/* dbus methods */
static GVariant *  handle_list_guards (BoltExported          *object,
                                       GVariant              *params,
                                       GDBusMethodInvocation *invocation,
                                       GError               **error);

static GVariant *  handle_force_power (BoltExported          *object,
                                       GVariant              *params,
                                       GDBusMethodInvocation *invocation,
                                       GError               **error);

struct _BoltPower
{
  BoltExported object;

  /* path to store run time data  */
  char  *runpath;
  GFile *statedir;
  GFile *statefile;

  /* connection to udev */
  BoltUdev *udev;

  /* the path to the sysfs device file,
   * or NULL if force power is unavailable */
  char          *path;
  BoltPowerState state;
  guint          reaper;

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
                         BOLT_TYPE_EXPORTED,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                power_initable_iface_init));

static void
bolt_power_finalize (GObject *object)
{
  BoltPower *power = BOLT_POWER (object);

  if (power->wait_id != 0)
    {
      g_source_remove (power->wait_id);
      bolt_power_wait_timeout (power);
    }

  if (power->reaper != 0)
    g_source_remove (power->reaper);

  g_clear_pointer (&power->runpath, g_free);
  g_clear_object (&power->statedir);
  g_clear_object (&power->statefile);
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
bolt_power_constructed (GObject *obj)
{
  BoltPower *power = BOLT_POWER (obj);

  G_OBJECT_CLASS (bolt_power_parent_class)->constructed (obj);

  if (power->runpath == NULL)
    power->runpath = g_strdup (bolt_get_runtime_directory ());
}

static void
bolt_power_class_init (BoltPowerClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  BoltExportedClass *exported_class = BOLT_EXPORTED_CLASS (klass);

  gobject_class->finalize = bolt_power_finalize;

  gobject_class->constructed  = bolt_power_constructed;
  gobject_class->get_property = bolt_power_get_property;
  gobject_class->set_property = bolt_power_set_property;

  power_props[PROP_RUNDIR] =
    g_param_spec_string ("rundir",
                         NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  power_props[PROP_STATEDIR] =
    g_param_spec_object ("statedir",
                         NULL, NULL,
                         G_TYPE_FILE,
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
                          "Supported", NULL,
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_NICK);

  power_props[PROP_STATE] =
    g_param_spec_enum ("state",
                       "State", NULL,
                       BOLT_TYPE_POWER_STATE,
                       BOLT_FORCE_POWER_UNSET,
                       G_PARAM_READABLE |
                       G_PARAM_STATIC_STRINGS);

  power_props[PROP_TIMEOUT] =
    g_param_spec_uint ("timeout",
                       "Timeout", NULL,
                       0, G_MAXINT, POWER_WAIT_TIMEOUT,
                       G_PARAM_READWRITE |
                       G_PARAM_CONSTRUCT_ONLY |
                       G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     power_props);

  bolt_exported_class_set_interface_info (exported_class,
                                          BOLT_DBUS_POWER_INTERFACE,
                                          BOLT_DBUS_GRESOURCE_PATH);

  bolt_exported_class_export_properties (exported_class,
                                         PROP_SUPPORTED,
                                         PROP_LAST,
                                         power_props);

  bolt_exported_class_export_method (exported_class,
                                     "ForcePower",
                                     handle_force_power);

  bolt_exported_class_export_method (exported_class,
                                     "ListGuards",
                                     handle_list_guards);
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
  g_autoptr(BoltGuard) guard = NULL;
  g_autofree char *statedir = NULL;
  BoltPower *power = BOLT_POWER (initable);
  gboolean on = FALSE;
  gboolean ok;
  guint guards;

  statedir = g_build_filename (power->runpath, DEFAULT_STATEDIR, NULL);
  power->statedir = g_file_new_for_path (statedir);
  power->statefile = g_file_get_child (power->statedir, STATE_FILENAME);

  bolt_info (LOG_TOPIC ("power"), "state located at: %s", statedir);

  ok = g_file_make_directory_with_parents (power->statedir, NULL, &err);
  if (!ok && !bolt_err_exists (err))
    bolt_warn_err (err, LOG_TOPIC ("power"),
                   "failed to create guarddir at %s", statedir);
  g_clear_error (&err);

  g_signal_connect_object (power->udev, "uevent",
                           (GCallback) handle_uevent_udev,
                           power, 0);

  ok = bolt_udev_detect_force_power (power->udev, &power->path, &err);
  if (!ok)
    {
      bolt_warn_err (err, LOG_TOPIC ("power"),
                     "failure while detecting force power");
      g_clear_error (&err);
    }

  bolt_msg (LOG_TOPIC ("power"), "force power support: %s",
            bolt_yesno (power->path != NULL));

  if (power->path == NULL)
    return TRUE;

  /* recover force power state */
  on = g_file_query_exists (power->statefile, NULL);

  /* recover saved power guards */
  ok = bolt_power_recover_guards (power, &err);
  if (!ok)
    {
      bolt_warn_err (err, LOG_TOPIC ("power"),
                     "failed to recover guards");
      g_clear_error (&err);
      /* NOT a critical failure */
    }

  /* enforce that our ON state is actually true */
  guards = g_hash_table_size (power->guards);
  if (on || guards > 0)
    {
      bolt_msg (LOG_TOPIC ("power"), "recovered state, on: %s, guards: %u",
                bolt_yesno (on), guards);

      bolt_info (LOG_TOPIC ("power"), "creating temporary power guard");

      guard = bolt_power_acquire (power, &err);
      if (guard == NULL)
        bolt_warn_err (err, LOG_TOPIC ("power"),
                       "failed to force-power controller");
      /* failures here are not critical */
    }

  return TRUE;
}

/* internal methods */
static gboolean
bolt_power_recover_guards (BoltPower *power,
                           GError   **error)
{
  g_autoptr(GError) err = NULL;
  g_autoptr(GDir) dir   = NULL;
  g_autofree char *statedir = NULL;
  const char *name;

  statedir = g_file_get_path (power->statedir);

  dir = g_dir_open (statedir, 0, error);
  if (dir == NULL)
    return FALSE;

  while ((name = g_dir_read_name (dir)) != NULL)
    {
      BoltGuard *guard;

      if (!g_str_has_suffix (name, ".guard"))
        continue;

      guard = bolt_guard_load (statedir, name, &err);

      if (guard == NULL)
        {
          bolt_warn_err (err, LOG_TOPIC ("power"),
                         "could not load guard '%s'", name);
          g_clear_error (&err);
          continue;
        }

      /* internal guards are discarded */
      if (bolt_streq (guard->who, "boltd"))
        {
          bolt_info (LOG_TOPIC ("power"), "ignoring boltd guard");
          continue;
        }
      else if (!bolt_pid_is_alive (guard->pid))
        {
          bolt_info (LOG_TOPIC ("power"),
                     "ignoring guard '%s for '%s': process dead",
                     guard->id, guard->who);
          bolt_guard_fifo_cleanup (guard);
          continue;
        }

      if (guard->fifo)
        {
          int fd;
          fd = bolt_guard_monitor (guard, &err);

          if (fd < 0)
            {
              bolt_warn_err (err, "could not monitor guard '%d'",
                             guard->id);
              g_clear_error (&err);
            }
          else
            {
              (void) close (fd);

              /* monitoring adds a reference that we don't want */
              g_object_unref (guard);
            }
        }

      bolt_info (LOG_TOPIC ("power"),
                 "guard '%s' for '%s' (pid %lu) recovered",
                 guard->id, guard->who, (gulong) guard->pid);

      g_signal_connect_object (guard, "released",
                               (GCallback) bolt_power_release,
                               power, G_CONNECT_SWAPPED);

      g_hash_table_insert (power->guards, guard->id, guard);
    }

  return TRUE;
}

static gboolean
bolt_power_wait_timeout (gpointer user_data)
{
  g_autoptr(GError) err = NULL;
  BoltPower *power = user_data;
  gboolean ok;

  if (power->path == NULL)
    /* force power support got removed while being used,
    * this was already complained about, so ignore it */
    return G_SOURCE_REMOVE;

  /* we just removed the last active guard */
  ok = bolt_power_switch_toggle (power, FALSE, &err);

  if (!ok)
    bolt_warn_err (err, LOG_TOPIC ("power"),
                   "failed to turn off force_power");

  power->wait_id = 0;
  return G_SOURCE_REMOVE;
}

static gboolean
bolt_power_reaper_timeout (gpointer user_data)
{
  g_autoptr(GList) keys = NULL;
  BoltPower *power = user_data;

  bolt_debug (LOG_TOPIC ("power"), "looking for dead processes");

  if (g_hash_table_size (power->guards) == 0)
    {
      bolt_debug (LOG_TOPIC ("power"), "reaper done");
      power->reaper = 0;
      return FALSE;
    }

  keys = g_hash_table_get_keys (power->guards);

  for (GList *l = keys; l != NULL; l = l->next)
    {
      gpointer id = l->data;
      BoltGuard *g = g_hash_table_lookup (power->guards, id);

      if (bolt_pid_is_alive (g->pid))
        continue;

      bolt_info (LOG_TOPIC ("power"),
                 "process '%lu' is dead, "
                 "releasing the guard '%s' for '%s'",
                 (gulong) g->pid, g->id, g->who);

      g_object_unref (g);
    }

  return TRUE;
}

static void
handle_uevent_thunderbolt (BoltPower          *power,
                           const char         *action,
                           struct udev_device *device)
{
/* no callback scheduled, nothing to do */
  if (power->wait_id == 0)
    return;

  /* only interested in added devices */
  if (!bolt_streq (action, "add"))
    return;

  /* if we are not in WAIT state, we don't
   * do anything, but if we are, we want
   * to reset the timeout */
  if (power->state != BOLT_FORCE_POWER_WAIT)
    return;

  bolt_info (LOG_TOPIC ("power"), "resetting timeout (uevent %s)",
             udev_device_get_syspath (device));

  bolt_power_timeout_reset (power);
}

static void
handle_uevent_wmi (BoltPower          *power,
                   const char         *action,
                   struct udev_device *device)
{
  g_autoptr(GError) err = NULL;
  g_autofree char *path = NULL;
  const char *name;
  const char *syspath;
  gboolean changed = FALSE;
  gboolean ok;

  syspath = udev_device_get_syspath (device);
  name = udev_device_get_sysname (device);

  bolt_debug (LOG_TOPIC ("power"), "uevent: wmi %s %s [%s %s]",
              action, name, syspath, power->path ? : "<unset>");

  if (!bolt_streq (action, "change"))
    return;

  ok = bolt_udev_detect_force_power (power->udev, &path, &err);

  if (!ok)
    {
      bolt_warn_err (err, LOG_TOPIC ("udev"),
                     "failed to detect force_power support");
      g_clear_error (&err);
    }

  if (path != NULL)
    {
      if (power->path == NULL || !bolt_streq (path, power->path))
        {
          bolt_set_str (&power->path, g_steal_pointer (&path));
          changed = TRUE;
        }
    }
  else if (path == NULL && power->path != NULL)
    {
      if (power->state > BOLT_FORCE_POWER_OFF)
        bolt_warn (LOG_TOPIC ("power"),
                   "force power supported removed while active");

      g_clear_pointer (&power->path, g_free);
      changed = TRUE;
    }

  if (changed)
    {
      /* if changed, we don't know our current state */
      power->state = BOLT_FORCE_POWER_UNSET;

      g_object_notify_by_pspec (G_OBJECT (power),
                                power_props[PROP_STATE]);
      g_object_notify_by_pspec (G_OBJECT (power),
                                power_props[PROP_SUPPORTED]);
    }
}

static void
handle_uevent_udev (BoltUdev           *udev,
                    const char         *action,
                    struct udev_device *device,
                    gpointer            user_data)
{
  BoltPower *power = BOLT_POWER (user_data);
  const char *subsystem;

  subsystem = udev_device_get_subsystem (device);

  if (bolt_streq (subsystem, "thunderbolt"))
    handle_uevent_thunderbolt (power, action, device);
  else if (bolt_streq (subsystem, "wmi"))
    handle_uevent_wmi (power, action, device);
}

static void
bolt_power_timeout_reset (BoltPower *power)
{
  if (power->wait_id > 0)
    g_source_remove (power->wait_id);

  power->wait_id = g_timeout_add (power->timeout,
                                  bolt_power_wait_timeout,
                                  power);

  if (power->state != BOLT_FORCE_POWER_WAIT)
    {
      power->state = BOLT_FORCE_POWER_WAIT;
      g_object_notify_by_pspec (G_OBJECT (power),
                                power_props[PROP_STATE]);
    }
}

static gboolean
bolt_power_switch_toggle (BoltPower *power,
                          gboolean   on,
                          GError   **error)
{
  g_autoptr(GError) err = NULL;
  g_autofree char *statepath = NULL;
  BoltPowerState state;
  gboolean ok;
  int fd;

  g_return_val_if_fail (BOLT_IS_POWER (power), FALSE);
  g_return_val_if_fail (power->path != NULL, FALSE);

  bolt_info (LOG_TOPIC ("power"), "setting force_power to %s",
             on ? "ON" : "OFF");

  fd = bolt_open (power->path, O_WRONLY, 0, error);
  if (fd < 0)
    return FALSE;

  ok = bolt_write_all (fd, on ? "1" : "0", 1, error);
  bolt_close (fd, NULL);

  if (!ok)
    return FALSE;

  statepath = g_file_get_path (power->statefile);

  if (on)
    {
      state = BOLT_FORCE_POWER_ON;
      fd = bolt_open (statepath, O_CREAT | O_TRUNC, 0666, &err);
      ok = fd > -1;
      if (ok)
        (void) close (fd);
    }
  else
    {
      state = BOLT_FORCE_POWER_OFF;
      ok = bolt_unlink (statepath, &err);
    }

  if (!ok)
    bolt_warn_err (err, "could not write force_power state-file");
  else
    bolt_debug (LOG_TOPIC ("power"), "wrote state %s to %s",
                bolt_power_state_to_string (state),
                statepath);

  power->state = state;
  g_object_notify_by_pspec (G_OBJECT (power),
                            power_props[PROP_STATE]);

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
bolt_power_release (BoltPower *power, BoltGuard *guard)
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

  bolt_power_timeout_reset (power);
}

/* dbus methods */
static GVariant *
handle_force_power (BoltExported          *object,
                    GVariant              *params,
                    GDBusMethodInvocation *invocation,
                    GError               **error)
{
  g_autoptr(BoltGuard) guard = NULL;
  g_autoptr(GUnixFDList) fds = NULL;
  g_autoptr(GError) err = NULL;
  g_autoptr(GVariant) res = NULL;
  GDBusConnection *con;
  BoltPower *power;
  const char *sender;
  const char *flags;
  const char *who;
  guint pid;
  int fd;

  power = BOLT_POWER (object);

  con = g_dbus_method_invocation_get_connection (invocation);
  sender = g_dbus_method_invocation_get_sender (invocation);

  res = g_dbus_connection_call_sync (con,
                                     "org.freedesktop.DBus",
                                     "/",
                                     "org.freedesktop.DBus",
                                     "GetConnectionUnixProcessID",
                                     g_variant_new ("(s)", sender),
                                     G_VARIANT_TYPE ("(u)"),
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1, NULL,
                                     &err);

  if (res == NULL)
    {
      g_set_error (error, BOLT_ERROR, BOLT_ERROR_FAILED,
                   "could not get pid of caller: %s",
                   err->message);
      return NULL;
    }

  g_variant_get (res, "(u)", &pid);

  g_variant_get (params, "(&s&s)", &who, &flags);

  /* TODO: log errors */
  guard = bolt_power_acquire_full (power, who, (pid_t) pid, error);
  if (guard == NULL)
    return NULL;

  fd = bolt_guard_monitor (guard, error);
  if (fd == -1)
    return NULL;

  fds = g_unix_fd_list_new_from_array (&fd, 1);
  g_dbus_method_invocation_return_value_with_unix_fd_list (invocation,
                                                           g_variant_new ("(h)"),
                                                           fds);
  return NULL;
}

static GVariant *
handle_list_guards (BoltExported          *object,
                    GVariant              *params,
                    GDBusMethodInvocation *invocation,
                    GError               **error)
{
  g_autoptr(GList) guards = NULL;
  BoltPower *power;
  GVariantBuilder b;

  power = BOLT_POWER (object);
  guards = bolt_power_list_guards (power);

  g_variant_builder_init (&b, G_VARIANT_TYPE ("a(ssu)"));

  for (GList *l = guards; l != NULL; l = l->next)
    {
      BoltGuard *g = BOLT_GUARD (l->data);
      g_variant_builder_add (&b, "(ssu)",
                             bolt_guard_get_id (g),
                             bolt_guard_get_who (g),
                             bolt_guard_get_pid (g));
    }

  return g_variant_new ("(a(ssu))", &b);
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

BoltGuard *
bolt_power_acquire (BoltPower *power,
                    GError   **error)
{
  const char *who = "boltd";

  return bolt_power_acquire_full (power, who, 0, error);
}

BoltGuard *
bolt_power_acquire_full (BoltPower  *power,
                         const char *who,
                         pid_t       pid,
                         GError    **error)
{
  g_autoptr(GError) err = NULL;
  g_autofree char *id = NULL;
  BoltGuard *guard;
  gboolean ok;

  g_return_val_if_fail (BOLT_IS_POWER (power), NULL);
  g_return_val_if_fail (who != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  if (power->path == NULL)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                           "force power not supported");
      return FALSE;
    }

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
      ok = bolt_power_switch_toggle (power, TRUE, &err);

      if (!ok)
        {
          bolt_error_propagate (error, &err);
          return NULL;
        }
    }

  if (pid == 0)
    pid = getpid ();

  guard = g_object_new (BOLT_TYPE_GUARD,
                        "id", id,
                        "who", who,
                        "pid", pid,
                        NULL);

  g_signal_connect_object (guard, "released",
                           (GCallback) bolt_power_release,
                           power, G_CONNECT_SWAPPED);

  /* NB: we don't take a ref here, because we want the guard to
   * act as RAII guard, i.e. when the client releases the last
   * reference to the guard, the "released" signal will be
   * triggered and thus bolt_power_release() will be called */
  g_hash_table_insert (power->guards, guard->id, guard);

  bolt_info (LOG_TOPIC ("power"), "guard '%s' for '%s' active",
             guard->id, guard->who);

  if (power->reaper == 0)
    power->reaper = g_timeout_add_seconds (POWER_REAPER_TIMEOUT,
                                           bolt_power_reaper_timeout,
                                           power);

  /* guard is saved so we can recover our state if we
   * were to crash or restarted */
  ok = bolt_guard_save (guard, power->statedir, &err);
  if (!ok)
    bolt_warn_err (err, LOG_TOPIC ("power"),
                   "could not save guard '%s'", guard->id);

  return guard;
}

GList *
bolt_power_list_guards (BoltPower *power)
{
  g_return_val_if_fail (BOLT_IS_POWER (power), NULL);

  return g_hash_table_get_values (power->guards);
}

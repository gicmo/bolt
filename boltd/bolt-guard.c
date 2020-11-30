/*
 * Copyright © 2020 Red Hat, Inc
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
#include "bolt-guard.h"
#include "bolt-io.h"
#include "bolt-log.h"
#include "bolt-str.h"
#include "bolt-unix.h"


typedef enum GuardState {
  GUARD_STATE_ACTIVE = 0,
  GUARD_STATE_RELEASED = 1
} GuardState;

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

  g_clear_pointer (&guard->path, g_free);
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
      bolt_debug (LOG_TOPIC ("guard"),
                  "not removing guard '%s' with active fifo",
                  guard->id);
      return;
    }

  ok = bolt_unlink (guard->path, &err);
  if (!ok)
    {
      bolt_warn_err (err, LOG_TOPIC ("guard"),
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
      bolt_warn_err (err, LOG_TOPIC ("guard"),
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
guard_has_event (GIOChannel  *source,
                 GIOCondition condition,
                 gpointer     data)
{
  BoltGuard *guard = data;

  bolt_info (LOG_TOPIC ("guard"), "got event for guard '%s' (%x)",
             guard->id, (guint) condition);
  guard->watch = 0;
  return FALSE;
}

static void
guard_watch_release (gpointer data)
{
  BoltGuard *guard = data;

  if (guard->state == GUARD_STATE_RELEASED)
    /* if we are already released, the FIFO was kept
     * alive on purpose, so do nothing here */
    return;

  if (guard->fifo == NULL)
    {
      bolt_bug (LOG_TOPIC ("guard"), "FIFO event but no FIFO");
      return;
    }

  bolt_guard_fifo_cleanup (guard);

  bolt_debug (LOG_TOPIC ("guard"),
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
                                      guard_has_event,
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

const char *
bolt_guard_get_path (BoltGuard *guard)
{
  g_return_val_if_fail (BOLT_IS_GUARD (guard), NULL);

  return guard->path;
}

const char *
bolt_guard_get_fifo (BoltGuard *guard)
{
  g_return_val_if_fail (BOLT_IS_GUARD (guard), NULL);

  return guard->fifo;
}

GPtrArray *
bolt_guard_recover (const char *statedir,
                    GError    **error)
{

  g_autoptr(GError) err = NULL;
  g_autoptr(GDir) dir   = NULL;
  g_autoptr(GPtrArray) guards = NULL;
  const char *name;

  g_return_val_if_fail (statedir != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  dir = g_dir_open (statedir, 0, error);
  if (dir == NULL)
    return NULL;

  guards = g_ptr_array_new_with_free_func (g_object_unref);

  while ((name = g_dir_read_name (dir)) != NULL)
    {
      g_autoptr(BoltGuard) guard = NULL;
      int fd;

      if (!g_str_has_suffix (name, ".guard"))
        continue;

      guard = bolt_guard_load (statedir, name, &err);

      if (guard == NULL)
        {
          bolt_warn_err (err, LOG_TOPIC ("guard"),
                         "could not load guard '%s'", name);
          g_clear_error (&err);
          continue;
        }

      /* internal guards are discarded */
      if (guard->fifo == NULL)
        {
          bolt_info (LOG_TOPIC ("guard"),
                     "ignoring guard '%s' for '%s': no fifo",
                     guard->id, guard->who);
          continue;
        }
      else if (!bolt_pid_is_alive (guard->pid))
        {
          bolt_info (LOG_TOPIC ("guard"),
                     "ignoring guard '%s' for '%s': process dead",
                     guard->id, guard->who);
          bolt_guard_fifo_cleanup (guard);
          continue;
        }

      fd = bolt_guard_monitor (guard, &err);

      if (fd < 0)
        {
          bolt_warn_err (err, "could not monitor guard '%d'",
                         guard->id);
          g_clear_error (&err);
          continue;
        }

      /* close the write side */
      (void) close (fd);

      /* monitoring adds a reference that we don't want */
      g_object_unref (guard);

      g_ptr_array_add (guards, guard);
      guard = NULL;
    }

  return g_steal_pointer (&guards);
}

gboolean
bolt_guard_save (BoltGuard *guard,
                 GFile     *guarddir,
                 GError   **error)
{
  g_autoptr(GFile) guardfile = NULL;
  g_autoptr(GKeyFile) kf = NULL;
  g_autofree char *path = NULL;
  g_autofree char *name = NULL;
  gboolean ok;

  g_return_val_if_fail (guard->path == NULL, FALSE);

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

BoltGuard *
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

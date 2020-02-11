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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *       Christian J. Kellner <christian@kellner.me>
 */

#include "config.h"

#include "bolt-reaper.h"

#include "bolt-log.h"
#include "bolt-unix.h"


#define REAPER_TIMEOUT 20 * 1000 // seconds


struct _BoltReaper
{
  GObject object;

  /*  */
  guint timeout;

  /*  */
  GHashTable *pids;
  guint       timeout_id;
};

enum {
  PROP_0,
  PROP_TIMEOUT,
  PROP_LAST
};

static GParamSpec *props[PROP_LAST] = { NULL, };

enum {
  PROCESS_DIED,
  SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = {0};

G_DEFINE_TYPE (BoltReaper,
               bolt_reaper,
               G_TYPE_OBJECT);


static void
bolt_reaper_finalize (GObject *object)
{
  BoltReaper *reaper = BOLT_REAPER (object);

  g_clear_handle_id (&reaper->timeout_id, g_source_remove);
  g_hash_table_unref (reaper->pids);

  G_OBJECT_CLASS (bolt_reaper_parent_class)->finalize (object);
}

static void
bolt_reaper_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  BoltReaper *reaper = BOLT_REAPER (object);

  switch (prop_id)
    {

    case PROP_TIMEOUT:
      g_value_set_uint (value, reaper->timeout);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bolt_reaper_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  BoltReaper *reaper = BOLT_REAPER (object);

  switch (prop_id)
    {
    case PROP_TIMEOUT:
      reaper->timeout = g_value_get_uint (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bolt_reaper_init (BoltReaper *reaper)
{
  reaper->pids = g_hash_table_new_full (g_direct_hash,
                                        g_direct_equal,
                                        NULL,
                                        g_free);
}

static void
bolt_reaper_class_init (BoltReaperClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = bolt_reaper_finalize;
  gobject_class->get_property = bolt_reaper_get_property;
  gobject_class->set_property = bolt_reaper_set_property;

  props[PROP_TIMEOUT] =
    g_param_spec_uint ("timeout",
                       "Timeout", NULL,
                       0, G_MAXINT, REAPER_TIMEOUT,
                       G_PARAM_READWRITE |
                       G_PARAM_CONSTRUCT_ONLY |
                       G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     props);


  signals[PROCESS_DIED] =
    g_signal_new ("process-died",
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE,
                  2, G_TYPE_UINT, G_TYPE_STRING);
}

static gboolean
bolt_reaper_timeout (gpointer user_data)
{
  BoltReaper *reaper = BOLT_REAPER (user_data);
  GHashTableIter iter;
  gpointer key, value;

  bolt_debug (LOG_TOPIC ("reaper"), "looking for dead processes");

  g_hash_table_iter_init (&iter, reaper->pids);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      g_autofree char *name = NULL;
      guint pid = GPOINTER_TO_UINT (key);
      bolt_debug (LOG_TOPIC ("reaper"), "checking '%u'", pid);

      if (bolt_pid_is_alive ((pid_t) pid))
        continue;

      bolt_info (LOG_TOPIC ("reaper"),
                 "process '%u' is dead",
                 pid);

      name = (char *) value;
      g_hash_table_iter_steal (&iter);

      g_signal_emit (reaper,
                     signals[PROCESS_DIED],
                     0,
                     pid, name);
    }

  if (g_hash_table_size (reaper->pids) == 0)
    {
      bolt_debug (LOG_TOPIC ("reaper"), "stopping");
      reaper->timeout_id = 0;
      return G_SOURCE_REMOVE;
    }

  return G_SOURCE_CONTINUE;
}

BoltReaper *
bolt_reaper_new (void)
{
  BoltReaper *reaper;

  reaper = g_object_new (BOLT_TYPE_REAPER, NULL);

  return reaper;
}

void
bolt_reaper_add_pid (BoltReaper *reaper,
                     guint       pid,
                     const char *name)
{
  gpointer p = GUINT_TO_POINTER (pid);

  g_return_if_fail (BOLT_IS_REAPER (reaper));

  g_hash_table_insert (reaper->pids, p, g_strdup (name));

  if (reaper->timeout_id != 0)
    return;

  reaper->timeout_id = g_timeout_add (reaper->timeout,
                                      bolt_reaper_timeout,
                                      reaper);

  bolt_info (LOG_TOPIC ("reaper"), "started");
}

gboolean
bolt_reaper_del_pid (BoltReaper *reaper,
                     guint       pid)
{
  gpointer p = GUINT_TO_POINTER (pid);

  g_return_val_if_fail (BOLT_IS_REAPER (reaper), FALSE);

  return g_hash_table_remove (reaper->pids, p);
}

gboolean
bolt_reaper_has_pid (BoltReaper *reaper,
                     guint       pid)
{
  gpointer p = GUINT_TO_POINTER (pid);

  g_return_val_if_fail (BOLT_IS_REAPER (reaper), FALSE);

  return g_hash_table_contains (reaper->pids, p);
}

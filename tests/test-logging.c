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

#include "bolt-device.h"
#include "bolt-error.h"
#include "bolt-str.h"
#include "bolt-term.h"

#include "bolt-log.h"

#include "bolt-daemon-resource.h"

#include <glib.h>
#include <gio/gio.h>
#include <glib/gprintf.h>

#include <locale.h>
#include <stdarg.h>
#include <stdio.h>

typedef struct _LogData
{
  GLogLevelFlags level;
  GHashTable    *fields;
} LogData;

typedef struct _TestLog
{
  LogData data;
} TestLog;

static void
test_log_setup (TestLog *tt, gconstpointer user_data)
{
  tt->data.fields = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  g_log_set_writer_func (g_log_writer_standard_streams, NULL, NULL);
}

static void
test_log_tear_down (TestLog *tt, gconstpointer user_data)
{
  g_hash_table_destroy (tt->data.fields);
}

static void
log_expectv (TestLog       *tt,
             GLogLevelFlags level,
             const char    *domain,
             const char    *message,
             va_list        args)
{
  const char *key;

  tt->data.level = level;

  g_hash_table_remove_all (tt->data.fields);

  if (domain)
    g_hash_table_insert (tt->data.fields,
                         g_strdup ("GLIB_DOMAIN"),
                         g_strdup (domain));

  if (message)
    g_hash_table_insert (tt->data.fields,
                         g_strdup ("MESSAGE"),
                         g_strdup (message));

  while ((key = va_arg (args, const char *)) != NULL)
    {
      const char *val = va_arg (args, const char *);
      g_hash_table_insert (tt->data.fields, g_strdup (key), g_strdup (val));
    }
}

static void
log_expect (TestLog       *tt,
            GLogLevelFlags level,
            const char    *domain,
            const char    *message,
            ...)
{
  va_list args;

  va_start (args, message);
  log_expectv (tt, level, domain, message, args);
  va_end (args);
}

static GLogWriterOutput
test_writer (GLogLevelFlags   log_level,
             const GLogField *fields,
             gsize            n_fields,
             gpointer         user_data)
{
  g_autoptr(GHashTable) index = NULL;
  LogData *data = user_data;
  GHashTableIter iter;
  gpointer k, v;

  index = g_hash_table_new (g_str_hash, g_str_equal);

  for (gsize i = 0; i < n_fields; i++)
    {
      const char *key = fields[i].key;
      const char *val = (const char *) fields[i].value;

      if (!g_hash_table_contains (data->fields, key))
        continue;

      g_hash_table_insert (index, (gpointer) key, (gpointer) val);
    }

  g_hash_table_iter_init (&iter, data->fields);
  while (g_hash_table_iter_next (&iter, &k, &v))
    {
      const char *key = k;
      const char *value = v;
      const char *have;

      g_assert_true (g_hash_table_contains (index, key));
      have = g_hash_table_lookup (index, key);
      g_assert_cmpstr (value, ==, have);
      //g_fprintf (stderr, "%s: %s vs %s\n", key, value, have);
    }

  return G_LOG_WRITER_HANDLED;
}

static void
test_log_basic (TestLog *tt, gconstpointer user_data)
{
  log_expect (tt, G_LOG_LEVEL_MESSAGE, "bolt-test", "test", NULL);

  g_log_set_writer_func (test_writer, &tt->data, NULL);
  bolt_log ("bolt-test", G_LOG_LEVEL_MESSAGE, "test");
}

static void
test_log_gerror (TestLog *tt, gconstpointer user_data)
{
  g_autoptr(GError) error = NULL;
  const char *domain = "bolt-gerror";
  GLogLevelFlags lvl = G_LOG_LEVEL_INFO;
  const char *msg;

  msg = "no udev";
  g_set_error_literal (&error, BOLT_ERROR, BOLT_ERROR_UDEV, msg);
  log_expect (tt, lvl, domain, NULL, "ERROR_MESSAGE", msg, NULL);

  g_log_set_writer_func (test_writer, &tt->data, NULL);
  bolt_log (domain, lvl, LOG_ERR (error), NULL);

  /* check we handle NULL GErrors without crashing */
  log_expect (tt, lvl, domain, NULL, "ERROR_MESSAGE", "unknown cause",
              BOLT_LOG_BUG_MARK, "*",
              NULL);
  bolt_log (domain, lvl, LOG_ERR (NULL), NULL);
}

static void
test_log_device (TestLog *tt, gconstpointer user_data)
{
  g_autoptr(BoltDevice) a = NULL;
  const char *domain = "bolt-device";
  const char *msg;
  GLogLevelFlags lvl;
  const char *uid_a = "fbc83890-e9bf-45e5-a777-b3728490989c";

  a = g_object_new (BOLT_TYPE_DEVICE,
                    "uid", uid_a,
                    "name", "Laptop",
                    "vendor", "GNOME.org",
                    "status", BOLT_STATUS_DISCONNECTED,
                    NULL);

  lvl = G_LOG_LEVEL_INFO;
  msg = "test device a";
  log_expect (tt, lvl, domain, msg,
              BOLT_LOG_DEVICE_UID, uid_a,
              NULL);

  g_log_set_writer_func (test_writer, &tt->data, NULL);
  bolt_log (domain, lvl, LOG_DEV (a), msg);
}

static void
test_log_macros (TestLog *tt, gconstpointer user_data)
{
  g_autoptr(GError) error = NULL;
  GLogLevelFlags lvl = G_LOG_LEVEL_INFO;

  const char *msg = "da steht ich nun ich armer test";

  g_log_set_writer_func (test_writer, &tt->data, NULL);

  log_expect (tt, G_LOG_LEVEL_MESSAGE, G_LOG_DOMAIN, msg, NULL);
  bolt_msg (msg);

  g_set_error_literal (&error, BOLT_ERROR, BOLT_ERROR_UDEV, msg);
  log_expect (tt, lvl, G_LOG_DOMAIN, NULL, "ERROR_MESSAGE", msg, NULL);
  bolt_warn_err (error, NULL);

  log_expect (tt, lvl, G_LOG_DOMAIN, NULL,
              BOLT_LOG_ERROR_MESSAGE, msg,
              NULL);

  bolt_log (G_LOG_DOMAIN, lvl,
            LOG_DIRECT (BOLT_LOG_ERROR_MESSAGE, msg),
            NULL);

  log_expect (tt, G_LOG_LEVEL_DEBUG, G_LOG_DOMAIN, msg,
              "CODE_FILE", __FILE__,
              "CODE_FUNC", G_STRFUNC,
              NULL);
  bolt_debug (msg);

  msg = "nasty bug";
  log_expect (tt, G_LOG_LEVEL_DEBUG, G_LOG_DOMAIN, msg,
              BOLT_LOG_TOPIC, "code",
              BOLT_LOG_BUG_MARK, "*",
              NULL);
  bolt_bug (msg);
}

int
main (int argc, char **argv)
{

  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  g_resources_register (bolt_daemon_get_resource ());

  g_test_add ("/logging/basic",
              TestLog,
              NULL,
              test_log_setup,
              test_log_basic,
              test_log_tear_down);

  g_test_add ("/logging/gerror",
              TestLog,
              NULL,
              test_log_setup,
              test_log_gerror,
              test_log_tear_down);

  g_test_add ("/logging/device",
              TestLog,
              NULL,
              test_log_setup,
              test_log_device,
              test_log_tear_down);

  g_test_add ("/logging/macros",
              TestLog,
              NULL,
              test_log_setup,
              test_log_macros,
              test_log_tear_down);

  return g_test_run ();
}

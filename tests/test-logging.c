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

#include "bolt-dbus.h"
#include "bolt-device.h"
#include "bolt-domain.h"
#include "bolt-error.h"
#include "bolt-str.h"
#include "bolt-term.h"

#include "bolt-log.h"

#include <glib.h>
#include <gio/gio.h>
#include <glib/gprintf.h>

#include <locale.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>


typedef struct _TestContext
{
  GLogWriterFunc logger;
  gpointer       logger_data;
} TestContext;

static GLogWriterOutput
test_context_logger (GLogLevelFlags   log_level,
                     const GLogField *fields,
                     gsize            n_fields,
                     gpointer         user_data)
{
  TestContext *ctx = user_data;

  g_assert_nonnull (ctx);
  g_assert_nonnull (ctx->logger);

  return ctx->logger (log_level, fields, n_fields, ctx->logger_data);
}

static void
test_context_set_logger (TestContext *ctx, GLogWriterFunc logger, gpointer user_data)
{
  ctx->logger = logger;
  ctx->logger_data = user_data;
}

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
  TestContext *ctx = (TestContext *) user_data;

  /* reset logger */
  test_context_set_logger (ctx, g_log_writer_standard_streams, NULL);

  tt->data.fields = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
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
  TestContext *ctx = (TestContext *) user_data;

  log_expect (tt, G_LOG_LEVEL_MESSAGE, "bolt-test", "test", NULL);

  test_context_set_logger (ctx, test_writer, &tt->data);
  bolt_log ("bolt-test", G_LOG_LEVEL_MESSAGE, "test");

  g_assert_nonnull (bolt_log_level_to_string (G_LOG_LEVEL_ERROR));
  g_assert_nonnull (bolt_log_level_to_string (G_LOG_LEVEL_CRITICAL));
  g_assert_nonnull (bolt_log_level_to_string (G_LOG_LEVEL_WARNING));
  g_assert_nonnull (bolt_log_level_to_string (G_LOG_LEVEL_MESSAGE));
  g_assert_nonnull (bolt_log_level_to_string (G_LOG_LEVEL_INFO));
  g_assert_nonnull (bolt_log_level_to_string (G_LOG_LEVEL_DEBUG));

  g_assert_nonnull (bolt_log_level_to_priority (G_LOG_LEVEL_ERROR));
  g_assert_nonnull (bolt_log_level_to_priority (G_LOG_LEVEL_CRITICAL));
  g_assert_nonnull (bolt_log_level_to_priority (G_LOG_LEVEL_WARNING));
  g_assert_nonnull (bolt_log_level_to_priority (G_LOG_LEVEL_MESSAGE));
  g_assert_nonnull (bolt_log_level_to_priority (G_LOG_LEVEL_INFO));
  g_assert_nonnull (bolt_log_level_to_priority (G_LOG_LEVEL_DEBUG));
}

static void
test_log_gerror (TestLog *tt, gconstpointer user_data)
{
  g_autoptr(GError) error = NULL;
  const char *domain = "bolt-gerror";
  GLogLevelFlags lvl = G_LOG_LEVEL_INFO;
  TestContext *ctx = user_data;
  const char *msg;

  msg = "no udev";
  g_set_error_literal (&error, BOLT_ERROR, BOLT_ERROR_UDEV, msg);
  log_expect (tt, lvl, domain, NULL, "ERROR_MESSAGE", msg, NULL);

  test_context_set_logger (ctx, test_writer, &tt->data);
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
  TestContext *ctx = user_data;

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

  test_context_set_logger (ctx, test_writer, &tt->data);

  bolt_log (domain, lvl, LOG_DEV (a), msg);
}

static void
test_log_macros (TestLog *tt, gconstpointer user_data)
{
  g_autoptr(GError) error = NULL;
  GLogLevelFlags lvl = G_LOG_LEVEL_INFO;
  TestContext *ctx = user_data;

  const char *msg = "da steht ich nun ich armer test";

  test_context_set_logger (ctx, test_writer, &tt->data);

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

static GLogWriterOutput
test_logger_stdstream (GLogLevelFlags   level,
                       const GLogField *fields,
                       gsize            n_fields,
                       gpointer         user_data)
{
  g_autoptr(BoltLogCtx) ctx = NULL;

  g_return_val_if_fail (fields != NULL, G_LOG_WRITER_UNHANDLED);
  g_return_val_if_fail (n_fields > 0, G_LOG_WRITER_UNHANDLED);

  ctx = bolt_log_ctx_acquire (fields, n_fields);

  if (ctx == NULL)
    return G_LOG_WRITER_UNHANDLED;

  return bolt_log_stdstream (ctx, level, 0);
}

static GLogWriterOutput
test_logger_journal (GLogLevelFlags   level,
                     const GLogField *fields,
                     gsize            n_fields,
                     gpointer         user_data)
{
  g_autoptr(BoltLogCtx) ctx = NULL;
  char message[2048] = {0, };
  const char *dom;

  g_return_val_if_fail (fields != NULL, G_LOG_WRITER_UNHANDLED);
  g_return_val_if_fail (n_fields > 0, G_LOG_WRITER_UNHANDLED);

  ctx = bolt_log_ctx_acquire (fields, n_fields);

  if (ctx == NULL)
    return G_LOG_WRITER_UNHANDLED;

  dom = blot_log_ctx_get_domain (ctx);
  if (dom != NULL)
    fprintf (stderr, "DOMAIN: %s", dom);

  bolt_log_fmt_journal (ctx, level, message, sizeof (message));
  fprintf (stderr, "%s\n", message);

  return G_LOG_WRITER_HANDLED;
}

static void
check_log_logger (TestLog *tt, gconstpointer user_data, gboolean journal)
{
  g_autoptr(GError) err = NULL;
  const char *msg = NULL;
  const char *uid1 = "884c6edd-7118-4b21-b186-b02d396ecca0";
  const char *uid2 = "884c6ede-7118-4b21-b186-b02d396ecca0";
  const char *uid3 = "884c6edf-7118-4b21-b186-b02d396ecca0";
  TestContext *ctx = (TestContext *) user_data;

  if (g_test_subprocess ())
    {
      g_autoptr(BoltDomain) dom = NULL;
      g_autoptr(BoltDevice) dev = NULL;

      if (journal)
        test_context_set_logger (ctx, test_logger_journal, tt);
      else
        test_context_set_logger (ctx, test_logger_stdstream, tt);

      dom = g_object_new (BOLT_TYPE_DOMAIN,
                          "id", "domain0",
                          "uid", uid1,
                          "bootacl", NULL,
                          NULL);

      dev = g_object_new (BOLT_TYPE_DEVICE,
                          "uid", uid2,
                          "name", "Laptop",
                          "vendor", "GNOME.org",
                          "status", BOLT_STATUS_DISCONNECTED,
                          NULL);

      msg = "no udev";
      g_set_error_literal (&err, BOLT_ERROR, BOLT_ERROR_UDEV, msg);
      bolt_warn_err (err, LOG_TOPIC ("the_topic"), "WARNUNG-1");

      bolt_log ("ck01", G_LOG_LEVEL_INFO, LOG_DEV (dev),
                "MESSAGE-%d", 1);

      bolt_log ("ck01", G_LOG_LEVEL_INFO, LOG_DOM (dom),
                "MESSAGE-%d", 2);

      bolt_log ("ck01", G_LOG_LEVEL_INFO, LOG_DEV_UID (uid3),
                "MESSAGE-%d", 3);

      g_warning ("WARNUNG-2");
      g_critical ("WARNUNG-3");

      g_log ("ck02", G_LOG_LEVEL_INFO, "MESSAGE-%d", 4);

      exit (0);
    }

  g_test_trap_subprocess (NULL, 0, 0);
  g_test_trap_assert_passed ();
  g_test_trap_assert_stderr ("*WARNUNG-1*");
  g_test_trap_assert_stderr ("*the_topic*");
  g_test_trap_assert_stderr ("*WARNUNG-2*");
  g_test_trap_assert_stderr ("*WARNUNG-3*");
  g_test_trap_assert_stderr ("*MESSAGE-1*");
  g_test_trap_assert_stderr ("*MESSAGE-2*");
  g_test_trap_assert_stderr ("*MESSAGE-3*");
  g_test_trap_assert_stderr ("*MESSAGE-3*");
  g_test_trap_assert_stderr ("*domain0*");
  g_test_trap_assert_stderr ("*884c6edd*");
  g_test_trap_assert_stderr ("*884c6ede*");
  g_test_trap_assert_stderr ("*884c6edf*");
  g_test_trap_assert_stderr ("*Laptop*");

  if (GPOINTER_TO_INT (user_data) == 1)
    {
      g_test_trap_assert_stderr ("*DOMAIN: ck01*");
      g_test_trap_assert_stderr ("*DOMAIN: ck02*");
    }
}

static void
test_log_logger_stdstream (TestLog *tt, gconstpointer user_data)
{
  check_log_logger (tt, user_data, FALSE);
}

static void
test_log_logger_journal (TestLog *tt, gconstpointer user_data)
{
  check_log_logger (tt, user_data, TRUE);
}

int
main (int argc, char **argv)
{
  TestContext test_ctx;

  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  g_log_set_writer_func (test_context_logger, &test_ctx, NULL);

  bolt_dbus_ensure_resources ();

  g_test_add ("/logging/basic",
              TestLog,
              &test_ctx,
              test_log_setup,
              test_log_basic,
              test_log_tear_down);

  g_test_add ("/logging/gerror",
              TestLog,
              &test_ctx,
              test_log_setup,
              test_log_gerror,
              test_log_tear_down);

  g_test_add ("/logging/device",
              TestLog,
              &test_ctx,
              test_log_setup,
              test_log_device,
              test_log_tear_down);

  g_test_add ("/logging/macros",
              TestLog,
              &test_ctx,
              test_log_setup,
              test_log_macros,
              test_log_tear_down);

  g_test_add ("/logging/logger/stdstream",
              TestLog,
              &test_ctx,
              test_log_setup,
              test_log_logger_stdstream,
              test_log_tear_down);

  g_test_add ("/logging/logger/journal",
              TestLog,
              &test_ctx,
              test_log_setup,
              test_log_logger_journal,
              test_log_tear_down);

  return g_test_run ();
}

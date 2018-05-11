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

#include "bolt-device.h"
#include "bolt-error.h"
#include "bolt-str.h"
#include "bolt-term.h"

#include "bolt-log.h"

#include <glib.h>
#include <gio/gio.h>
#include <glib/gprintf.h>

#include <stdarg.h>
#include <stdio.h>

/* stolen from glib */
static const char *
log_level_to_priority (GLogLevelFlags log_level)
{
  if (log_level & G_LOG_LEVEL_ERROR)
    return "3";
  else if (log_level & G_LOG_LEVEL_CRITICAL)
    return "4";
  else if (log_level & G_LOG_LEVEL_WARNING)
    return "4";
  else if (log_level & G_LOG_LEVEL_MESSAGE)
    return "5";
  else if (log_level & G_LOG_LEVEL_INFO)
    return "6";
  else if (log_level & G_LOG_LEVEL_DEBUG)
    return "7";

  /* Default to LOG_NOTICE for custom log levels. */
  return "5";
}

static const char *
log_level_to_string (GLogLevelFlags log_level)
{
  if (log_level & G_LOG_LEVEL_ERROR)
    return "error";
  else if (log_level & G_LOG_LEVEL_CRITICAL)
    return "critical";
  else if (log_level & G_LOG_LEVEL_WARNING)
    return "warning";
  else if (log_level & G_LOG_LEVEL_MESSAGE)
    return "message";
  else if (log_level & G_LOG_LEVEL_INFO)
    return "info";
  else if (log_level & G_LOG_LEVEL_DEBUG)
    return "debug";

  return "user";
}

static FILE *
log_level_to_file (GLogLevelFlags log_level)
{
  return log_level & G_LOG_LEVEL_DEBUG ? stdout : stderr;
}

#define internal_error(fmt, ...) g_fprintf (stderr, "log-ERROR: " fmt "\n", __VA_ARGS__)

static const char *
bolt_color_for (FILE *out, const char *color)
{
  int fd = fileno (out);

  return g_log_writer_supports_color (fd) ? color : "";
}

struct _BoltLogCtx
{
  BoltDevice   *device;
  const GError *error;

  /* standard fields */
  GLogField *self;
  GLogField *message;
  GLogField *priority;
  GLogField *domain;
  GLogField *topic;

  gboolean   is_bug;

  /* field storage */
  gsize     n_fields;
  GLogField fields[32];

  /* heap or stack */
  gboolean allocated;
};

static gboolean
bolt_log_ctx_next_field (BoltLogCtx *ctx, GLogField **next)
{
  const gsize maxfields = G_N_ELEMENTS (ctx->fields);
  gboolean res = TRUE;
  gsize n; /* safe to use as index */

  /* make sure we are ALWAYS within bounds */
  n     = MIN (ctx->n_fields, maxfields - 1);
  *next = ctx->fields + n;
  res   = ctx->n_fields < maxfields;
  ctx->n_fields += 1;

  if (!res)
    internal_error ("fields overflow. '%s' dropped",
                    ctx->fields[n].key);

  return res;
}

#define BOLT_LOG_CTX_KEY "BOLT_LOG_CONTEXT"

static gsize
bolt_log_ctx_finish (BoltLogCtx *ctx)
{
  GLogField *self;
  gboolean ok;

  ok = bolt_log_ctx_next_field (ctx, &self);
  if (!ok)
    {
      const gsize maxfields = sizeof (ctx->fields);
      gsize i = ctx->n_fields - maxfields;
      ctx->n_fields = maxfields;
      internal_error ("overflow of %" G_GSIZE_FORMAT, i);
    }

  self->key = BOLT_LOG_CTX_KEY;
  self->value = ctx;
  self->length = 0;
  ctx->self = self;

  return ctx->n_fields;
}

static gboolean
bolt_log_ctx_find_field (const BoltLogCtx *ctx,
                         const char       *name,
                         const GLogField **out)
{
  g_return_val_if_fail (ctx != NULL, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (out != NULL, FALSE);

  for (gsize i = 0; i < ctx->n_fields; i++)
    {
      const GLogField *field = ctx->fields + i;
      if (bolt_streq (field->key, name))
        {
          *out = field;
          return TRUE;
        }
    }

  return FALSE;
}

static void
handle_device_field (BoltLogCtx *ctx,
                     const char *key,
                     gpointer    ptr)
{
  BoltDevice *dev = BOLT_DEVICE (ptr);
  BoltStatus status;
  GLogField *field;

  g_return_if_fail (BOLT_IS_DEVICE (dev));
  ctx->device = dev;

  bolt_log_ctx_next_field (ctx, &field);
  field->key = BOLT_LOG_DEVICE_UID;
  field->value = bolt_device_get_uid (dev);
  field->length = -1;

  bolt_log_ctx_next_field (ctx, &field);
  field->key = BOLT_LOG_DEVICE_NAME;
  field->value = bolt_device_get_name (dev);
  field->length = -1;

  bolt_log_ctx_next_field (ctx, &field);
  status = bolt_device_get_status (dev);
  field->key = BOLT_LOG_DEVICE_STATE;
  field->value = bolt_status_to_string (status);
  field->length = -1;
}

static void
handle_gerror_field (BoltLogCtx *ctx,
                     const char *key,
                     gpointer    ptr)
{
  const GError *error = ptr;
  GLogField *field;
  GError fallback = {
    .domain = BOLT_ERROR,
    .code = BOLT_ERROR_FAILED,
    .message = (char *) "unknown cause",
  };

  ctx->error = error;
  if (error == NULL)
    {
      error = &fallback;
      ctx->is_bug = TRUE;
    }

  bolt_log_ctx_next_field (ctx, &field);
  field->key =  BOLT_LOG_ERROR_DOMAIN;
  field->value = g_quark_to_string (error->domain);
  field->length = -1;

  bolt_log_ctx_next_field (ctx, &field);
  field->key = BOLT_LOG_ERROR_CODE;
  field->value = &error->code;
  field->length = sizeof (gint);

  bolt_log_ctx_next_field (ctx, &field);
  field->key = BOLT_LOG_ERROR_MESSAGE;
  field->value = error->message;
  field->length = -1;
}

static void
handle_topic_field (BoltLogCtx *ctx,
                    const char *key,
                    gpointer    ptr)
{
  const char *value = ptr;
  GLogField *field;

  bolt_log_ctx_next_field (ctx, &field);

  field->key =  BOLT_LOG_TOPIC;
  field->value = value;
  field->length = -1;

  ctx->topic = field;

  if (bolt_streq ((const char *) ptr, "code"))
    ctx->is_bug = TRUE;
}

static gboolean
handle_special_field (BoltLogCtx *ctx,
                      const char *key,
                      gpointer    ptr)
{
  gboolean handled = TRUE;

  key++; /* remove the special key indicator */

  if (g_str_has_prefix (key, "device"))
    handle_device_field (ctx, key, ptr);
  else if (g_str_equal (key, "error"))
    handle_gerror_field (ctx, key, ptr);
  else if (g_str_equal (key, "topic"))
    handle_topic_field (ctx, key, ptr);
  else

    handled = FALSE;

  return handled;
}

static gboolean
handle_passthrough_field (BoltLogCtx *ctx,
                          const char *key,
                          const char *val)
{
  GLogField *field;

  key++; /* remove the pass-through key indicator */

  bolt_log_ctx_next_field (ctx, &field);

  field->key = key;
  field->value = val;
  field->length = -1;

  return TRUE;
}

static void
add_bug_marker (BoltLogCtx *ctx)
{
  GLogField *field;

  bolt_log_ctx_next_field (ctx, &field);

  field->key = BOLT_LOG_BUG_MARK;
  field->value = "*";
  field->length = -1;
}

void
bolt_log (const char    *domain,
          GLogLevelFlags level,
          ...)
{
  va_list args;

  va_start (args, level);
  bolt_logv (domain, level, args);
  va_end (args);
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"

void
bolt_logv (const char    *domain,
           GLogLevelFlags level,
           va_list        args)
{
  BoltLogCtx ctx = {NULL, };
  char message[1024] = {0, };
  const char *key;

  bolt_log_ctx_next_field (&ctx, &ctx.message);
  bolt_log_ctx_next_field (&ctx, &ctx.priority);
  bolt_log_ctx_next_field (&ctx, &ctx.domain);

  while ((key = va_arg (args, const char *)) != NULL)
    {
      gboolean handled;

      if (*key == LOG_SPECIAL_CHAR)
        {
          gpointer ptr = va_arg (args, gpointer);
          handled = handle_special_field (&ctx, key, ptr);
        }
      else if (*key == LOG_PASSTHROUGH_CHAR)
        {
          const char *val = va_arg (args, const char *);
          handled = handle_passthrough_field (&ctx, key, val);
        }
      else
        {
          break;
        }

      if (!handled)
        internal_error ("unknown field: %s", key);
    }

  g_vsnprintf (message, sizeof (message), key ? : "", args);
  ctx.message->key = "MESSAGE";
  ctx.message->value = message;
  ctx.message->length = -1;

  ctx.priority->key = "PRIORITY";
  ctx.priority->value = log_level_to_priority (level);
  ctx.priority->length = -1;

  ctx.domain->key = "GLIB_DOMAIN";
  ctx.domain->value = domain ? : "boltd";
  ctx.domain->length = -1;

  if (ctx.is_bug)
    add_bug_marker (&ctx);

  bolt_log_ctx_finish (&ctx);

  /* pass it to the normal log mechanisms;
   * this should handle aborting on fatal
   * error messages for us. */
  g_log_structured_array (level, ctx.fields, ctx.n_fields);
}
#pragma GCC diagnostic pop

static char *
format_device_id (BoltDevice *device, char *buffer, gsize len, int size)
{
  static const int u = 13;
  const char *uid;
  const char *name;
  int n;

  uid = bolt_device_get_uid (device);
  name = bolt_device_get_name (device);

  n = MAX (0, size - u);
  g_snprintf (buffer, len, "%-.*s-%-*.*s", u, uid, n, n, name);
  return buffer;
}

#define TIME_MAXFMT 255
GLogWriterOutput
bolt_log_stdstream (const BoltLogCtx *ctx,
                    GLogLevelFlags    log_level,
                    guint             flags)
{
  FILE *out = log_level_to_file (log_level);
  const char *normal = bolt_color_for (out, ANSI_NORMAL);
  const char *gray = bolt_color_for (out, ANSI_HIGHLIGHT_BLACK);
  const char *blue = bolt_color_for (out, ANSI_BLUE);
  const char *fg = normal;
  const char *message;
  const GLogField *f;
  char the_time[TIME_MAXFMT];
  time_t now;
  struct tm *tm;

  g_return_val_if_fail (ctx != NULL, G_LOG_WRITER_UNHANDLED);
  g_return_val_if_fail (ctx->message != NULL, G_LOG_WRITER_UNHANDLED);

  time (&now);
  tm = localtime (&now);

  if (tm && strftime (the_time, sizeof (the_time), "%T", tm) > 0)
    g_fprintf (out, "%s%s%s ", gray, the_time, normal);

  if (log_level == G_LOG_LEVEL_CRITICAL ||
      log_level == G_LOG_LEVEL_ERROR)
    fg = bolt_color_for (out, ANSI_RED);
  else if (log_level == G_LOG_LEVEL_WARNING)
    fg = bolt_color_for (out, ANSI_YELLOW);
  else if (log_level == G_LOG_LEVEL_DEBUG)
    fg = gray;

  if (ctx->device)
    {
      char name[64];

      format_device_id (ctx->device, name, sizeof (name), 30);
      g_fprintf (out, "[%s%s%s] ", blue, name, normal);
    }
  else if (bolt_log_ctx_find_field (ctx, BOLT_LOG_DEVICE_UID, &f))
    {
      const char *uid = f->value;

      g_fprintf (out, "[%s%.13s %17s%s] ", blue, uid, " ", fg);
    }

  if (ctx->topic)
    g_fprintf (out, "%s%s%s: ", blue, (const char *) ctx->topic->value, fg);

  message = ctx->message->value;
  g_fprintf (out, "%s%s%s", fg, message, normal);

  if (ctx->error)
    {
      const char *yellow = bolt_color_for (out, ANSI_YELLOW);
      const char *msg = ctx->error->message;
      if (strlen (message) == 0)
        {
          const char *lvl = log_level_to_string (log_level);
          g_fprintf (out, "%s%s%s", fg, lvl, normal);
        }
      g_fprintf (out, ": %s%s%s", yellow, msg, normal);
    }

  g_fprintf (out, "\n");
  fflush (out);

  return G_LOG_WRITER_HANDLED;
}

static int
bolt_cat_printf (char **buffer, gsize *size, const char *fmt, ...)
{
  va_list args;
  char *p = *buffer;
  gsize s = *size;
  int n;

  va_start (args, fmt);
  n = g_vsnprintf (p, s, fmt, args);
  va_end (args);

  /* TODO: check n */
  *size = s - n;
  *buffer = p + n;

  return n;
}

GLogWriterOutput
bolt_log_journal (const BoltLogCtx *ctx,
                  GLogLevelFlags    log_level,
                  guint             flags)
{
  GLogWriterOutput res;
  const char *m;
  const char *old = NULL;
  char message[2048];
  gsize size = sizeof (message);
  char *p = message;
  const GLogField *f;

  if (ctx == NULL || ctx->message == NULL)
    return G_LOG_WRITER_UNHANDLED;

  if (ctx->device)
    {
      char name[64];

      format_device_id (ctx->device, name, sizeof (name), 40);
      bolt_cat_printf (&p, &size, "[%s] ", name);
    }
  else if (bolt_log_ctx_find_field (ctx, BOLT_LOG_DEVICE_UID, &f))
    {
      const char *uid = f->value;
      bolt_cat_printf (&p, &size, "[%.13s %27s] ", uid, " ");
    }

  if (ctx->topic)
    bolt_cat_printf (&p, &size, "%s: ", (const char *) ctx->topic->value);

  m = ctx->message->value;
  bolt_cat_printf (&p, &size, "%s", m);

  if (ctx->error)
    {
      const GError *error = ctx->error;
      const char *msg = error->message;
      if (strlen (m) == 0)
        bolt_cat_printf (&p, &size, "%s", log_level_to_string (log_level));

      bolt_cat_printf (&p, &size, ": %s", msg);
    }

  old = ctx->message->value;
  ctx->message->value = message;

  res = g_log_writer_journald (log_level,
                               ctx->fields,
                               ctx->n_fields,
                               NULL);

  ctx->message->value = old;

  return res;
}

BoltLogCtx *
bolt_log_ctx_acquire (const GLogField *fields,
                      gsize            n)
{
  BoltLogCtx *ctx;

  if (bolt_streq (fields[n - 1].key, BOLT_LOG_CTX_KEY))
    {
      ctx = (BoltLogCtx *) fields[n - 1].value;

      /* sanity check */
      if (ctx->message == NULL)
        return NULL;

      return ctx;
    }

  ctx = g_new0 (BoltLogCtx, 1);
  ctx->allocated = TRUE;

  bolt_log_ctx_next_field (ctx, &ctx->message);
  bolt_log_ctx_next_field (ctx, &ctx->priority);
  bolt_log_ctx_next_field (ctx, &ctx->domain);

  for (gsize i = 0; i < n; i++)
    {
      GLogField *field = (GLogField *) &fields[i];

      if (bolt_streq (field->key, "MESSAGE"))
        ctx->message = field;
      else if (bolt_streq (field->key, "GLIB_DOMAIN"))
        ctx->domain = field;
      else if (bolt_streq (field->key, "PRIORITY"))
        ctx->priority = field;
    }

  if (ctx->message == NULL)
    {
      g_free (ctx);
      return NULL;
    }

  bolt_log_ctx_finish (ctx);
  return ctx;
}

gboolean
bolt_log_ctx_set_id (BoltLogCtx *ctx,
                     const char *id)
{
  if (ctx->self == NULL)
    return FALSE;
  else if (ctx->self->length != 0)
    return FALSE;

  ctx->self->value = id;
  ctx->self->length = -1;

  return TRUE;
}

void
bolt_log_ctx_free (BoltLogCtx *ctx)
{
  if (ctx == NULL || ctx->allocated == FALSE)
    return;

  g_free (ctx);
}

const char *
blot_log_ctx_get_domain (BoltLogCtx *ctx)
{
  return ctx->domain ? ctx->domain->value : NULL;
}

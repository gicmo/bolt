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
#include "bolt-domain.h"
#include "bolt-error.h"
#include "bolt-macros.h"
#include "bolt-rnd.h"
#include "bolt-str.h"
#include "bolt-term.h"

#include "bolt-log.h"

#include <glib.h>
#include <gio/gio.h>
#include <glib/gprintf.h>

#include <stdarg.h>
#include <stdio.h>

/* mapping taking from glib */
typedef struct BoltLogLevel
{
  GLogLevelFlags code;
  const char    *prio;
  const char    *name;
} BoltLogLevel;

BoltLogLevel known_levels[] = {
  {G_LOG_LEVEL_ERROR,    "3", "error"},
  {G_LOG_LEVEL_CRITICAL, "4", "critical"},
  {G_LOG_LEVEL_WARNING,  "4", "warning"},
  {G_LOG_LEVEL_MESSAGE,  "5", "message"},
  {G_LOG_LEVEL_INFO,     "6", "info"},
  {G_LOG_LEVEL_DEBUG,    "7", "debug"}
};

const char *
bolt_log_level_to_priority (GLogLevelFlags log_level)
{
  for (gsize i = 0; i < G_N_ELEMENTS (known_levels); i++)
    if (known_levels[i].code & log_level)
      return known_levels[i].prio;

  /* Default to LOG_NOTICE for custom log levels. */
  return "5";
}

const char *
bolt_log_level_to_string (GLogLevelFlags log_level)
{
  for (gsize i = 0; i < G_N_ELEMENTS (known_levels); i++)
    if (known_levels[i].code & log_level)
      return known_levels[i].name;

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
handle_domain_field (BoltLogCtx *ctx,
                     const char *key,
                     gpointer    ptr)
{
  BoltDomain *dom = BOLT_DOMAIN (ptr);
  const char *name;
  GLogField *field;

  g_return_if_fail (BOLT_IS_DOMAIN (dom));

  bolt_log_ctx_next_field (ctx, &field);
  field->key = BOLT_LOG_DOMAIN_UID;
  field->value = bolt_domain_get_uid (dom);
  field->length = -1;

  name = bolt_domain_get_id (dom);
  if (name == NULL)
    return;

  bolt_log_ctx_next_field (ctx, &field);
  field->key = BOLT_LOG_DOMAIN_NAME;
  field->value = name;
  field->length = -1;
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

struct SpecialField
{
  const char *name;
  void        (*handler) (BoltLogCtx *ctx,
                          const char *key,
                          gpointer    ptr);
} special_fields[] = {
  {"device", handle_device_field},
  {"domain", handle_domain_field},
  {"error",  handle_gerror_field},
  {"topic",  handle_topic_field}
};

static gboolean
handle_special_field (BoltLogCtx *ctx,
                      const char *key,
                      gpointer    ptr)
{
  gboolean handled = FALSE;

  key++; /* remove the special key indicator */

  for (gsize i = 0; i < G_N_ELEMENTS (special_fields); i++)
    {
      if (g_str_equal (key, special_fields[i].name))
        {
          special_fields[i].handler (ctx, key, ptr);
          handled = TRUE;
        }
    }

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
  ctx.priority->value = bolt_log_level_to_priority (level);
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
format_uid_name (const char *uid,
                 const char *name,
                 char       *buffer,
                 gsize       len,
                 int         size)
{
  static const int u = 13;
  int n;

  n = MAX (0, size - u);
  g_snprintf (buffer, len, "%-.*s-%-*.*s", u, uid, n, n, name);
  return buffer;
}

static char *
format_device_id (BoltDevice *device, char *buffer, gsize len, int size)
{
  const char *uid;
  const char *name;

  uid = bolt_device_get_uid (device);
  name = bolt_device_get_name (device);

  format_uid_name (uid, name, buffer, len, size);

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
  struct tm lt;
  struct tm *tm;
  time_t now;

  g_return_val_if_fail (ctx != NULL, G_LOG_WRITER_UNHANDLED);
  g_return_val_if_fail (ctx->message != NULL, G_LOG_WRITER_UNHANDLED);

  time (&now);
  tm = localtime_r (&now, &lt);

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
  else if (bolt_log_ctx_find_field (ctx, BOLT_LOG_DOMAIN_UID, &f))
    {
      const char *uid = f->value;
      const char *name = "domain?";
      char ident[64];

      if (bolt_log_ctx_find_field (ctx, BOLT_LOG_DOMAIN_NAME, &f))
        name = f->value;

      format_uid_name (uid, name, ident, sizeof (ident), 30);
      g_fprintf (out, "[%s%s%s] ", blue, ident, fg);
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
          const char *lvl = bolt_log_level_to_string (log_level);
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

void
bolt_log_fmt_journal (const BoltLogCtx *ctx,
                      GLogLevelFlags    log_level,
                      char             *message,
                      gsize             size)
{
  const char *m;
  char *p = message;
  const GLogField *f;

  g_return_if_fail (ctx != NULL && ctx->message != NULL);

  if (ctx->device)
    {
      char name[64];

      format_device_id (ctx->device, name, sizeof (name), 40);
      bolt_cat_printf (&p, &size, "[%s] ", name);
    }
  else if (bolt_log_ctx_find_field (ctx, BOLT_LOG_DOMAIN_UID, &f))
    {
      const char *uid = f->value;
      const char *name = "domain?";
      char ident[64];

      if (bolt_log_ctx_find_field (ctx, BOLT_LOG_DOMAIN_NAME, &f))
        name = f->value;

      format_uid_name (uid, name, ident, sizeof (ident), 40);
      bolt_cat_printf (&p, &size, "[%s] ", ident);
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
        bolt_cat_printf (&p, &size, "%s", bolt_log_level_to_string (log_level));

      bolt_cat_printf (&p, &size, ": %s", msg);
    }
}

GLogWriterOutput
bolt_log_journal (const BoltLogCtx *ctx,
                  GLogLevelFlags    log_level,
                  guint             flags)
{
  GLogWriterOutput res;
  char message[2048];
  GLogField msg = {"MESSAGE", message, -1};

  g_return_val_if_fail (ctx != NULL, G_LOG_WRITER_UNHANDLED);
  g_return_val_if_fail (ctx->message != NULL, G_LOG_WRITER_UNHANDLED);

  bolt_log_fmt_journal (ctx, log_level, message, sizeof (message));

  *ctx->message = msg;

  res = g_log_writer_journald (log_level,
                               ctx->fields,
                               ctx->n_fields,
                               NULL);
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

  for (gsize i = 0; i < n; i++)
    {
      GLogField *field = (GLogField *) &fields[i];

      if (bolt_streq (field->key, "MESSAGE"))
        {
          bolt_log_ctx_next_field (ctx, &ctx->message);
          *(ctx->message) = *field;
        }
      else if (bolt_streq (field->key, "GLIB_DOMAIN"))
        {
          bolt_log_ctx_next_field (ctx, &ctx->domain);
          *(ctx->domain) = *field;
        }
      else if (bolt_streq (field->key, "PRIORITY"))
        {
          bolt_log_ctx_next_field (ctx, &ctx->priority);
          *(ctx->priority) = *field;
        }
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

void
bolt_log_gen_id (char id[BOLT_LOG_MSG_IDLEN])
{
  guint8 data[16] = {0, };
  static const char ch[16] = "0123456789abcdef";

  bolt_get_random_data (&data, sizeof (data));

  for (guint i = 0; i < 16; i++)
    {
      const guint8 b = data[i];

      g_assert_cmpint ((b >> 4),  <, sizeof (ch));

      id[i * 2]     = ch[b >> 4];
      id[i * 2 + 1] = ch[b & 15];
    }

  id[32] = '\0';
}

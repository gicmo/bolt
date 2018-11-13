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

#pragma once

#include "bolt-names.h"

#include <gio/gio.h>

#include <stdarg.h>


G_BEGIN_DECLS

#define LOG_SPECIAL_CHAR '@'
#define LOG_PASSTHROUGH_CHAR '_'

#define LOG_DIRECT(k, v) "_" k, v
#define LOG_DEV(device) "@device", device
#define LOG_DOM(domain) "@domain", domain
#define LOG_ERR(error) "@error", error
#define LOG_TOPIC(topic) "@topic", topic
#define LOG_DOM_UID(uid) LOG_DIRECT (BOLT_LOG_DOMAIN_UID, uid)
#define LOG_DEV_UID(uid) LOG_DIRECT (BOLT_LOG_DEVICE_UID, uid)
#define LOG_MSG_ID(msg_id) LOG_DIRECT ("MESSAGE_ID", msg_id)
#define LOG_ID(id) LOG_MSG_ID (BOLT_LOG_MSG_ID_ ## id)


#define bolt_debug(...) bolt_log (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG,                 \
                                  LOG_DIRECT ("CODE_FILE", __FILE__),               \
                                  LOG_DIRECT ("CODE_LINE", G_STRINGIFY (__LINE__)), \
                                  LOG_DIRECT ("CODE_FUNC", G_STRFUNC),              \
                                  __VA_ARGS__)

#define bolt_info(...) bolt_log (G_LOG_DOMAIN, G_LOG_LEVEL_INFO,                   \
                                 LOG_DIRECT ("CODE_FILE", __FILE__),               \
                                 LOG_DIRECT ("CODE_LINE", G_STRINGIFY (__LINE__)), \
                                 LOG_DIRECT ("CODE_FUNC", G_STRFUNC),              \
                                 __VA_ARGS__)

#define bolt_msg(...) bolt_log (G_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE,                \
                                LOG_DIRECT ("CODE_FILE", __FILE__),                \
                                LOG_DIRECT ("CODE_LINE", G_STRINGIFY (__LINE__)),  \
                                LOG_DIRECT ("CODE_FUNC", G_STRFUNC),               \
                                __VA_ARGS__)

#define bolt_warn(...) bolt_log (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,                 \
                                 LOG_DIRECT ("CODE_FILE", __FILE__),                \
                                 LOG_DIRECT ("CODE_LINE", G_STRINGIFY (__LINE__)),  \
                                 LOG_DIRECT ("CODE_FUNC", G_STRFUNC),               \
                                 __VA_ARGS__)

#define bolt_critical(...) bolt_log (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL,                \
                                     LOG_DIRECT ("CODE_FILE", __FILE__),                \
                                     LOG_DIRECT ("CODE_LINE", G_STRINGIFY (__LINE__)),  \
                                     LOG_DIRECT ("CODE_FUNC", G_STRFUNC),               \
                                     __VA_ARGS__)

#define bolt_error(...)  G_STMT_START {                                  \
    bolt_log (G_LOG_DOMAIN, G_LOG_LEVEL_ERROR,                          \
              LOG_DIRECT ("CODE_FILE", __FILE__),                       \
              LOG_DIRECT ("CODE_LINE", G_STRINGIFY (__LINE__)),         \
              LOG_DIRECT ("CODE_FUNC", G_STRFUNC),                      \
              __VA_ARGS__);                                             \
    for (;; ) { }                                                       \
} G_STMT_END

#define bolt_warn_err(e, ...) bolt_log (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,                 \
                                        LOG_DIRECT ("CODE_FILE", __FILE__),                \
                                        LOG_DIRECT ("CODE_LINE", G_STRINGIFY (__LINE__)),  \
                                        LOG_DIRECT ("CODE_FUNC", G_STRFUNC),               \
                                        LOG_ERR (e),                                       \
                                        __VA_ARGS__)

#define bolt_warn_enum_unhandled(the_enum, the_value) G_STMT_START {  \
    bolt_log (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL,                   \
              LOG_DIRECT ("CODE_FILE", __FILE__),                   \
              LOG_DIRECT ("CODE_LINE", G_STRINGIFY (__LINE__)),     \
              LOG_DIRECT ("CODE_FUNC", G_STRFUNC),                  \
              LOG_TOPIC ("code"),                                   \
              "unhandled value of enum " #the_enum "%d",            \
              (int) the_value);                                     \
} G_STMT_END

#define bolt_bug(...) bolt_log (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL,               \
                                LOG_DIRECT ("CODE_FILE", __FILE__),               \
                                LOG_DIRECT ("CODE_LINE", G_STRINGIFY (__LINE__)), \
                                LOG_DIRECT ("CODE_FUNC", G_STRFUNC),              \
                                LOG_TOPIC ("code"),                               \
                                __VA_ARGS__)

void               bolt_logv (const char    *domain,
                              GLogLevelFlags level,
                              va_list        args);

void               bolt_log (const char    *domain,
                             GLogLevelFlags level,
                             ...);

/* consumer functions */

const char *       bolt_log_level_to_priority (GLogLevelFlags log_level);

const char *       bolt_log_level_to_string (GLogLevelFlags log_level);

typedef struct _BoltLogCtx BoltLogCtx;

BoltLogCtx *       bolt_log_ctx_acquire (const GLogField *fields,
                                         gsize            n);

gboolean           bolt_log_ctx_set_id (BoltLogCtx *ctx,
                                        const char *id);

void               bolt_log_ctx_free (BoltLogCtx *ctx);

const char *       blot_log_ctx_get_domain (BoltLogCtx *ctx);

void               bolt_log_fmt_journal (const BoltLogCtx *ctx,
                                         GLogLevelFlags    log_level,
                                         char             *message,
                                         gsize             size);

GLogWriterOutput   bolt_log_stdstream (const BoltLogCtx *ctx,
                                       GLogLevelFlags    log_level,
                                       guint             flags);

GLogWriterOutput    bolt_log_journal (const BoltLogCtx *ctx,
                                      GLogLevelFlags    log_level,
                                      guint             flags);

void               bolt_log_gen_id (char id[BOLT_LOG_MSG_IDLEN]);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (BoltLogCtx, bolt_log_ctx_free);

G_END_DECLS

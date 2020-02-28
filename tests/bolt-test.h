/*
 * Copyright © 2018-2019 Red Hat, Inc
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

#include <glib.h>

G_BEGIN_DECLS

typedef char *BoltTmpDir;

BoltTmpDir    bolt_tmp_dir_make (const char *pattern,
                                 GError    **error);

void          bolt_tmp_dir_destroy (BoltTmpDir dir);

G_DEFINE_AUTO_CLEANUP_FREE_FUNC (BoltTmpDir, bolt_tmp_dir_destroy, NULL)

/* helper macro */

/* *INDENT-OFF* */
#define bolt_assert_strv_equal(a, b, n) G_STMT_START {                                \
    const GStrv sa__ = (a);                                                           \
    const GStrv sb__ = (b);                                                           \
    guint al__ = a != NULL ? g_strv_length (sa__) : 0;                                \
    guint bl__ = b != NULL ? g_strv_length (sb__) : 0;                                \
    if (n > 0) {                                                                      \
        al__ = MIN ((guint) n, al__);                                                 \
        bl__ = MIN ((guint) n, bl__);                                                 \
      }                                                                               \
    if (al__ != bl__)                                                                 \
      g_assertion_message_cmpnum (G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC,        \
                                  "len(" #a ") == len(" #b ")",                       \
                                  (long double) al__, "!=", (long double) bl__, 'i'); \
    else                                                                              \
      for (guint il__ = 0; il__ < al__; il__++)                                       \
        if (g_strcmp0 (sa__[il__], sb__[il__]) == 0) {; } else {                      \
          g_autofree char *va__ = NULL;                                               \
          va__ = g_strdup_printf (#a "[%u] != " #b "[%u]", il__, il__ );              \
          g_assertion_message_cmpstr (G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC,    \
                                      va__, sa__[il__], "!=", sb__[il__]);            \
        }                                                                             \
    } G_STMT_END

#define skip_test_if(condition, message) G_STMT_START {    \
    if (condition)                                         \
      {                                                    \
        g_test_skip (message);                             \
        return;                                            \
      }                                                    \
  } G_STMT_END

#define skip_test_unless(condition, message) skip_test_if(!(condition), message)

/* *INDENT-ON* */

/* Notification Socket */
typedef struct NotifySocket NotifySocket;

NotifySocket * notify_socket_new (void);
void           notify_socket_free (NotifySocket *ns);

char *         notify_socket_revmsg (NotifySocket *ns,
                                     gboolean      queue);
void           notify_socket_enable_watch (NotifySocket *ns);
void           notify_socket_set_environment (NotifySocket *ns);
void           notify_socket_make_pollfd (NotifySocket *ns,
                                          GPollFD      *fd);

/* Version parsing, checking */

typedef struct BoltVersion_ BoltVersion;
struct BoltVersion_
{
  union
  {
    struct
    {
      int major;
      int minor;
      int patch;
    };

    int triplet[3];
  };

  char *suffix;
};

#define BOLT_VERSION_INIT(ma, mi, pa) {.major = (ma), .minor = (mi), .patch = (pa), .suffix = NULL}

gboolean   bolt_version_parse (const char  *str,
                               BoltVersion *version,
                               GError     **error);

void       bolt_version_clear (BoltVersion *version);

int        bolt_version_compare (BoltVersion *a,
                                 BoltVersion *b);

gboolean   bolt_version_check (BoltVersion *version,
                               int          major,
                               int          minor,
                               int          patch);

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (BoltVersion, bolt_version_clear);

gboolean  bolt_check_kernel_version (int major,
                                     int minor);

gboolean  bolt_test_run_main_loop (GMainLoop *loop,
                                   guint      timeout_seconds,
                                   gboolean   exit_on_timeout,
                                   GError   **error);

G_END_DECLS

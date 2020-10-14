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

#pragma once

#include <glib.h>
#include <string.h>

G_BEGIN_DECLS

void bolt_erase_n (void *data,
                   gsize n);
void bolt_str_erase (char *str);
void bolt_str_erase_clear (char **str);

#define bolt_streq(s1, s2) (g_strcmp0 ((s1), (s2)) == 0)
#define bolt_strcaseeq(s1, s2) (g_ascii_strcasecmp ((s1), (s2)) == 0)

GStrv    bolt_strv_from_ptr_array (GPtrArray **array);
GStrv    bolt_strv_make_n (guint       size,
                           const char *init);
gsize    bolt_strv_length (char * const *strv);
guint    bolt_gstrv_length0 (const GStrv strv);
char **  bolt_strv_contains (GStrv       haystack,
                             const char *needle);
gboolean bolt_strv_equal (const GStrv a,
                          const GStrv b);

GHashTable * bolt_strv_diff (const GStrv before,
                             const GStrv after);

char ** bolt_strv_rotate_left (char **strv);
void    bolt_strv_permute (char **strv);

#define  bolt_strv_isempty(strv) ((strv) == NULL || *(strv) == NULL)

gboolean bolt_uuidv_check (const GStrv uuidv,
                           gboolean    empty_ok,
                           GError    **error);

#define bolt_strzero(str) (str == NULL || *str == '\0')

#define bolt_yesno(val) val ? "yes" : "no"
#define bolt_okfail(val) (!!val) ? "ok" : "fail"

char *bolt_strdup_validate (const char *string);

char *bolt_strstrip (char *string);

gboolean bolt_str_parse_as_int (const char *str,
                                gint       *ret,
                                GError    **error);

gboolean bolt_str_parse_as_uint (const char *str,
                                 guint      *ret,
                                 GError    **error);

gboolean bolt_str_parse_as_uint64 (const char *str,
                                   guint64    *ret,
                                   GError    **error);

gboolean bolt_str_parse_as_uint32 (const char *str,
                                   guint32    *ret,
                                   GError    **error);

gboolean bolt_str_parse_as_boolean (const char *str,
                                    gboolean   *ret,
                                    GError    **error);

/* replacing string pointers, like g_set_object, g_set_error ... */
static inline gboolean
bolt_set_str (char **target, char *str)
{
  char *ptr;

  g_return_val_if_fail (target != NULL, FALSE);

  ptr = *target;

  if (ptr == str)
    return FALSE;

  g_free (ptr);
  *target = str;

  return TRUE;
}

#define bolt_set_strdup(target, str) \
  bolt_set_str (target, g_strdup (str))

gboolean bolt_set_strdup_printf (char      **target,
                                 const char *fmt,
                                 ...) G_GNUC_PRINTF (2, 3);

gint     bolt_comparefn_strcmp (gconstpointer a,
                                gconstpointer b);

G_END_DECLS

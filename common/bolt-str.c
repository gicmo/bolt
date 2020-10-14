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

#include "bolt-error.h"
#include "bolt-macros.h"
#include "bolt-str.h"

#include <gio/gio.h>

#include <errno.h>
#include <string.h>

typedef void (* zero_fn_t) (void  *s,
                            size_t n);
void
bolt_erase_n (void *data, gsize n)
{
#if !HAVE_FN_EXPLICIT_BZERO
  #warning no explicit bzero, using fallback
  static volatile zero_fn_t explicit_bzero = bzero;
#endif

  explicit_bzero (data, n);
}

void
bolt_str_erase (char *str)
{
  if (str == NULL)
    return;

  bolt_erase_n (str, strlen (str));
}

void
bolt_str_erase_clear (char **str)
{
  g_return_if_fail (str != NULL);
  if (*str == NULL)
    return;

  bolt_str_erase (*str);
  g_free (*str);
  *str = NULL;
}

GStrv
bolt_strv_from_ptr_array (GPtrArray **array)
{
  GPtrArray *a;

  if (array == NULL || *array == NULL)
    return NULL;

  a = *array;

  if (a->len == 0 || a->pdata[a->len - 1] != NULL)
    g_ptr_array_add (a, NULL);

  *array = NULL;
  return (GStrv) g_ptr_array_free (a, FALSE);
}

GStrv
bolt_strv_make_n (guint size, const char *init)
{
  GPtrArray *a;

  a = g_ptr_array_sized_new (size + 1);
  for (guint i = 0; i < size; i++)
    g_ptr_array_add (a, g_strdup (init));

  return bolt_strv_from_ptr_array (&a);
}

gsize
bolt_strv_length (char * const *strv)
{
  gsize l = 0;

  if (strv == NULL)
    return 0;

  while (*strv++ != NULL)
    l++;

  return l;
}

guint
bolt_gstrv_length0 (const GStrv strv)
{
  if (strv == NULL)
    return 0;

  return g_strv_length ((char **) strv);
}

char **
bolt_strv_contains (GStrv haystack, const char *needle)
{
  g_return_val_if_fail (needle != NULL, NULL);

  if (haystack == NULL)
    return NULL;

  for (char **iter = haystack; *iter != NULL; iter++)
    if (bolt_streq (*iter, needle))
      return iter;

  return NULL;
}

gboolean
bolt_strv_equal (const GStrv a, const GStrv b)
{
  guint na, nb;

  if (a == b)
    return TRUE;

  na = a == NULL ? 0 : g_strv_length (a);
  nb = b == NULL ? 0 : g_strv_length (b);

  if (na != nb)
    return FALSE;

  for (guint i = 0; i < na; i++)
    if (!bolt_streq (a[i], b[i]))
      return FALSE;

  return TRUE;
}

GHashTable *
bolt_strv_diff (const GStrv before,
                const GStrv after)
{
  GHashTable *diff = NULL;

  diff = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  /* build an index of everything in 'before', marking is as
   * deleted for now
   */
  for (char **iter = before; iter && *iter; iter++)
    {
      char *key = *iter;

      if (bolt_strzero (key))
        continue;

      g_hash_table_insert (diff, g_strdup (key), GINT_TO_POINTER ('-'));
    }

  for (char **iter = after; iter && *iter; iter++)
    {
      char *key = *iter;
      gboolean have;

      if (bolt_strzero (*iter))
        continue;

      have = g_hash_table_contains (diff, key);

      /* if *iter is also in before, we have it in both GStrv and
       * thus we remove it from the diff; otherwise it is a new
       * string and we add it to the diff
       */
      if (have)
        g_hash_table_remove (diff, key);
      else
        g_hash_table_insert (diff, g_strdup (key),
                             GINT_TO_POINTER ('+'));
    }

  return diff;
}

char **
bolt_strv_rotate_left (char **strv)
{
  char *start;
  char **prev;
  char **iter;

  if (strv == NULL || *strv == NULL)
    return NULL;

  start = *strv; /* remember the first element */
  for (prev = strv, iter = strv + 1; *iter; prev = iter, iter++)
    *prev = *iter;

  *prev = start;
  return prev;
}

void
bolt_strv_permute (char **strv)
{
  guint n;

  if (strv == NULL || *strv == NULL)
    return;

  n = bolt_strv_length (strv);

  g_return_if_fail (n > 0);

  /* Knuth shuffles */
  for (guint i = 0; i < (n - 1) && strv[i]; i++)
    {
      /* random int k, with i ≤ k < n,
       * i.e. random_int_range returns k ∈ [i, n-1] */
      gsize k = g_random_int_range (i, n);

      bolt_swap (strv[i], strv[k]);
    }
}

gboolean
bolt_uuidv_check (const GStrv uuidv,
                  gboolean    empty_ok,
                  GError    **error)
{
  if (bolt_strv_isempty (uuidv))
    {
      if (empty_ok)
        return TRUE;

      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                           "provided uuid array is empty");
      return FALSE;
    }

  for (char * const *p = uuidv; *p; p++)
    {
      const char *uuid = *p;

      if (bolt_strzero (uuid) && empty_ok)
        continue;

      if (!g_uuid_string_is_valid (uuid))
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                       "entry '%s' is not a valid UUID", uuid);
          return FALSE;
        }
    }

  return TRUE;
}

char *
bolt_strdup_validate (const char *string)
{
  g_autofree char *str = NULL;
  gboolean ok;
  gsize l;

  if (string == NULL)
    return NULL;

  str = g_strdup (string);
  str = g_strstrip (str);

  l = strlen (str);
  if (l == 0)
    return NULL;

  ok = g_utf8_validate (str, l, NULL);

  if (!ok)
    return NULL;

  return g_steal_pointer (&str);
}

char *
bolt_strstrip (char *string)
{
  char *str;

  if (string == NULL)
    return NULL;

  str = g_strstrip (string);

  if (strlen (str) == 0)
    g_clear_pointer (&str, g_free);

  return str;
}

gboolean
bolt_str_parse_as_int (const char *str,
                       gint       *ret,
                       GError    **error)
{
  char *end;
  gint64 val;

  g_return_val_if_fail (str != NULL, -1);

  errno = 0;
  val = g_ascii_strtoll (str, &end, 0);

  if (val == 0 && (errno != 0 || str == end))
    {
      if (errno == 0)
        errno = EINVAL;

      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "failed to parse '%s' as unsigned integer", str);
      return FALSE;
    }
  else if (((val == G_MAXINT64 || val == G_MININT64) && errno == ERANGE) ||
           val > G_MAXINT || val < G_MININT)
    {
      if (errno == 0)
        errno = ERANGE;

      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "parsing '%s' overflows unsigned integer", str);
      return FALSE;
    }

  if (ret)
    *ret = (gint) val;

  return TRUE;
}

gboolean
bolt_str_parse_as_uint (const char *str,
                        guint      *ret,
                        GError    **error)
{
  guint64 val;
  gboolean ok;

  ok = bolt_str_parse_as_uint64 (str, &val, error);
  if (!ok)
    return FALSE;

  if (val > G_MAXUINT)
    {
      errno = ERANGE;
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "parsing '%s' overflows uint", str);
      return FALSE;
    }

  if (ret)
    *ret = (guint) val;

  return TRUE;
}

gboolean
bolt_str_parse_as_uint64 (const char *str,
                          guint64    *ret,
                          GError    **error)
{
  guint64 val;
  char *end;

  g_return_val_if_fail (str != NULL, FALSE);

  errno = 0;
  val = g_ascii_strtoull (str, &end, 0);

  if (val == 0 && (errno != 0 || str == end))
    {
      if (errno == 0)
        errno = EINVAL;

      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "failed to parse '%s' as unsigned integer", str);
      return FALSE;
    }
  else if (val == G_MAXUINT64 && errno == ERANGE)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "parsing '%s' overflows unsigned integer", str);
      return FALSE;
    }

  if (ret)
    *ret = val;

  return TRUE;
}

gboolean
bolt_str_parse_as_uint32 (const char *str,
                          guint32    *ret,
                          GError    **error)
{
  gboolean ok;
  guint64 val;

  ok = bolt_str_parse_as_uint64 (str, &val, error);
  if (!ok)
    return FALSE;

  if (val > G_MAXUINT32)
    {
      errno = ERANGE;
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "parsing '%s' overflows uint32", str);
      return FALSE;
    }

  if (ret)
    *ret = (guint32) val;

  return TRUE;
}

gboolean
bolt_str_parse_as_boolean (const char *str,
                           gboolean   *ret,
                           GError    **error)
{
  struct
  {
    const char *s;
    gboolean    v;
  } table[] = {
    {"1",    TRUE}, {"0",     FALSE},
    /* true, false */
    {"t",    TRUE}, {"f",     FALSE},
    {"true", TRUE}, {"false", FALSE},
    /* yes, no */
    {"y",    TRUE}, {"n",     FALSE},
    {"yes",  TRUE}, {"no",    FALSE},
    /* on, off */
    {"on",   TRUE}, {"off",   FALSE},
  };

  g_return_val_if_fail (str != NULL, FALSE);

  for (gsize i = 0; i < G_N_ELEMENTS (table); i++)
    {
      if (bolt_strcaseeq (table[i].s, str))
        {
          if (ret)
            *ret = table[i].v;

          return TRUE;
        }
    }

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
               "failed to parse '%s' as boolean", str);
  return FALSE;
}

gboolean
bolt_set_strdup_printf (char      **target,
                        const char *fmt,
                        ...)
{
  va_list args;
  char *str;

  g_return_val_if_fail (target != NULL, FALSE);
  g_return_val_if_fail (fmt != NULL, FALSE);

  va_start (args, fmt);
  str = g_strdup_vprintf (fmt, args);
  va_end (args);

  return bolt_set_str (target, str);
}

gint
bolt_comparefn_strcmp (gconstpointer a,
                       gconstpointer b)
{
  const char * const *astrv = a;
  const char * const *bstrv = b;

  return g_strcmp0 (*astrv, *bstrv);
}

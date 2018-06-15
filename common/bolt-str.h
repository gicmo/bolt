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

#define bolt_streq(s1, s2) (g_strcmp0 (s1, s2) == 0)

GStrv   bolt_strv_from_ptr_array (GPtrArray **array);

#define bolt_yesno(val) val ? "yes" : "no"
#define bolt_okfail(val) (!!val) ? "ok" : "fail"

char *bolt_strdup_validate (const char *string);

char *bolt_strstrip (char *string);

gboolean bolt_str_parse_as_int (const char *str,
                                gint       *ret);

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

G_END_DECLS

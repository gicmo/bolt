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

#include "bolt-str.h"

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

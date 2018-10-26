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

#include "bolt-str.h"
#include "bolt-term.h"

#include "boltctl-uidfmt.h"

#include <gio/gio.h>
#include <math.h>

char *
bolt_uuid_format (const char *uuid,
                  int         fmt)
{
  g_autofree char *tmp = NULL;
  guint op;
  int len;
  char *res;

  if (uuid == NULL)
    return NULL;

  g_return_val_if_fail (fmt > -1, NULL);

  op = fmt & 0xFF;

  switch (op)
    {
    case BOLT_UID_FORMAT_FULL:
      res = g_strdup (uuid);
      break;

    case BOLT_UID_FORMAT_SHORT:
      res = g_strdup_printf ("%.13s%s", uuid, bolt_glyph (ELLIPSIS));
      break;

    case BOLT_UID_FORMAT_ALIAS:
      tmp = g_compute_checksum_for_string (G_CHECKSUM_SHA1, uuid, -1);
      res = g_strdup_printf ("%.8s-%.4s-%.4s-%.4s-%.12s",
                             tmp, tmp + 8, tmp + 12, tmp + 16, tmp + 20);
      break;

    case BOLT_UID_FORMAT_LEN:
      len = MIN (fmt >> 8, 36);
      res = g_strdup_printf ("%.*s%s", len, uuid,
                             (len < 36 ? bolt_glyph (ELLIPSIS) : ""));
      break;

    default:
      res = NULL;
      g_warning ("unknown uuid format enum value: %d", fmt);
    }

  return res;
}


int
bolt_uuid_format_from_string (const char *str,
                              GError    **error)
{
  char *end;
  gint64 val;

  if (bolt_streq (str, "short"))
    return BOLT_UID_FORMAT_SHORT;
  else if (bolt_streq (str, "full"))
    return BOLT_UID_FORMAT_FULL;
  else if (bolt_streq (str, "alias"))
    return BOLT_UID_FORMAT_ALIAS;

  val = g_ascii_strtoll (str, &end, 10);

  if (str == end)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "uuid format: unknown style: %s", str);
      return -1;
    }
  else if (val < 1)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "uuid format: invalid number: %s", str);
      return -1;
    }
  else if (val > 36)
    {
      return BOLT_UID_FORMAT_FULL;
    }

  return BOLT_UID_FORMAT_LEN | (val << 8);
}


/*  */
int uuids_format;

int
format_uid_init (const char *str,
                 GError    **error)
{
  int fmt;

  g_return_val_if_fail (str != NULL, -1);

  fmt = bolt_uuid_format_from_string (str, error);
  if (fmt < 0)
    return fmt;

  uuids_format = fmt;

  return fmt;
}

const char *
format_uid (const char *uid)
{
  if (uid == NULL)
    uid = "<null>";

  return bolt_uuid_format (uid, uuids_format);
}

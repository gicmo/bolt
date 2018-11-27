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
#include <stdlib.h>

char *
bolt_uuid_format (const char *uuid,
                  const char *salt,
                  int         fmt)
{
  g_autoptr(GChecksum) chk = NULL;
  const char *tmp = NULL;
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
      chk = g_checksum_new (G_CHECKSUM_SHA1);
      if (salt)
        g_checksum_update (chk, (const guchar *) salt, -1);
      g_checksum_update (chk, (const guchar *) uuid, -1);

      tmp = g_checksum_get_string (chk);
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
GHashTable *uuids_table;
gboolean uuids_cleanup;
char *uuids_salt;

static void
format_uid_cleanup (void)
{
  g_clear_pointer (&uuids_table, g_hash_table_unref);
  g_clear_pointer (&uuids_salt, g_free);
  uuids_format = 0;
}

#define MACHINE_ID_PATH "/etc/machine-id"
#define BOOT_ID_PATH "/proc/sys/kernel/random/boot_id"

static char *
get_salt (void)
{
  char *salt = NULL;
  gboolean ok;

  ok = g_file_get_contents (MACHINE_ID_PATH, &salt, NULL, NULL);

  if (ok && !bolt_strzero (salt))
    {
      g_debug ("using machine-id as salt");
      return salt;
    }

  ok = g_file_get_contents (BOOT_ID_PATH, &salt, NULL, NULL);
  if (ok && !bolt_strzero (salt))
    {
      g_debug ("using boot-id as salt");
      return salt;
    }

  g_debug ("using PACKAGE_VERSION as pseudo-salt :(");
  return g_strdup (PACKAGE_VERSION);
}

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

  if (uuids_table)
    g_hash_table_unref (uuids_table);

  uuids_table = g_hash_table_new_full (g_str_hash, g_str_equal,
                                       g_free, g_free);

  uuids_salt = get_salt ();

  if (!uuids_cleanup)
    {
      uuids_cleanup = TRUE;
      atexit (format_uid_cleanup);
    }

  return fmt;
}

const char *
format_uid (const char *uid)
{
  const char *res;
  char *key, *val;

  if (uid == NULL)
    uid = "<null>";

  res = g_hash_table_lookup (uuids_table, uid);

  if (res != NULL)
    return res;

  key = g_strdup (uid);
  val = bolt_uuid_format (uid, uuids_salt, uuids_format);
  g_hash_table_insert (uuids_table, key, val);

  return val;
}

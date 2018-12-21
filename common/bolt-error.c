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

#include "bolt-names.h"

#include <gio/gio.h>

/**
 * SECTION:bolt-error
 * @Title: Error codes
 *
 */

static const GDBusErrorEntry bolt_error_entries[] = {
  {BOLT_ERROR_FAILED,     BOLT_DBUS_NAME ".Error.Failed"},
  {BOLT_ERROR_UDEV,       BOLT_DBUS_NAME ".Error.UDev"},
  {BOLT_ERROR_NOKEY,      BOLT_DBUS_NAME ".Error.NoKey"},
  {BOLT_ERROR_BADKEY,     BOLT_DBUS_NAME ".Error.BadKey"},
  {BOLT_ERROR_CFG,        BOLT_DBUS_NAME ".Error.Cfg"},
  {BOLT_ERROR_BADSTATE,   BOLT_DBUS_NAME ".Error.BadState"},
  {BOLT_ERROR_AUTHCHAIN,  BOLT_DBUS_NAME ".Error.AuthChain"},
};


GQuark
bolt_error_quark (void)
{
  static volatile gsize quark_volatile = 0;

  g_dbus_error_register_error_domain ("bolt-error-quark",
                                      &quark_volatile,
                                      bolt_error_entries,
                                      G_N_ELEMENTS (bolt_error_entries));
  return (GQuark) quark_volatile;
}

gboolean
bolt_err_notfound (const GError *error)
{
  return g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ||
         g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_NOENT) ||
         g_error_matches (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND) ||
         g_error_matches (error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_GROUP_NOT_FOUND);
}

gboolean
bolt_err_exists (const GError *error)
{
  return g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS) ||
         g_error_matches (error, G_FILE_ERROR, G_FILE_ERROR_EXIST);
}

gboolean
bolt_err_inval (const GError *error)
{
  return g_error_matches (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
}

gboolean
bolt_err_cancelled (const GError *error)
{
  return g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
}

gboolean
bolt_err_badstate (const GError *error)
{
  return g_error_matches (error, BOLT_ERROR, BOLT_ERROR_BADSTATE);
}

gboolean
bolt_err_nokey (const GError *error)
{
  return g_error_matches (error, BOLT_ERROR, BOLT_ERROR_NOKEY);
}

gboolean
bolt_error_propagate (GError **dest,
                      GError **source)
{
  GError *src;

  g_return_val_if_fail (source != NULL, FALSE);

  src = *source;

  if (src == NULL)
    return TRUE;

  g_propagate_error (dest, src);
  *source = NULL;

  return FALSE;
}

gboolean
bolt_error_propagate_stripped (GError **dest,
                               GError **source)
{
  GError *src;

  g_return_val_if_fail (source != NULL, FALSE);

  src = *source;

  if (src == NULL)
    return TRUE;

  if (g_dbus_error_is_remote_error (src))
    g_dbus_error_strip_remote_error (src);

  g_propagate_error (dest, g_steal_pointer (source));
  return FALSE;
}

gboolean
bolt_error_for_errno (GError    **error,
                      gint        err_no,
                      const char *format,
                      ...)
{
  va_list ap;
  int code;

  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  g_return_val_if_fail (format != NULL, FALSE);

  if (err_no == 0)
    return TRUE;

  if (error == NULL)
    return FALSE;

  code = g_io_error_from_errno (err_no);

  va_start (ap, format);
  *error = g_error_new_valist (G_IO_ERROR, code, format, ap);
  va_end (ap);

  return FALSE;
}

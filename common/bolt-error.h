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

G_BEGIN_DECLS

/**
 * BoltError:
 * @BOLT_ERROR_FAILED: Generic error code
 * @BOLT_ERROR_UDEV: UDev error
 * @BOLT_ERROR_NOKEY: Key for authorization is missing
 * @BOLT_ERROR_BADKEY: The key is invalid
 * @BOLT_ERROR_CFG: Configuration is invalid
 * @BOLT_ERROR_BADSTATE: Device is in the wrong state
 * @BOLT_ERROR_AUTHCHAIN: Interrupted authorization chain
 *
 * Error codes used inside Bolt.
 */
typedef enum {
  BOLT_ERROR_FAILED = 0,
  BOLT_ERROR_UDEV,
  BOLT_ERROR_NOKEY,
  BOLT_ERROR_BADKEY,
  BOLT_ERROR_CFG,
  BOLT_ERROR_BADSTATE,
  BOLT_ERROR_AUTHCHAIN,
} BoltError;


GQuark bolt_error_quark (void);
#define BOLT_ERROR (bolt_error_quark ())

/* helper function to check for certain error types */
gboolean bolt_err_notfound (const GError *error);
gboolean bolt_err_exists (const GError *error);
gboolean bolt_err_inval (const GError *error);
gboolean bolt_err_cancelled (const GError *error);
gboolean bolt_err_badstate (const GError *error);
gboolean bolt_err_nokey (const GError *error);

gboolean bolt_error_propagate (GError **dest,
                               GError **source);
gboolean bolt_error_propagate_stripped (GError **dest,
                                        GError **source);

gboolean bolt_error_for_errno (GError    **error,
                               gint        err_no,
                               const char *format,
                               ...) G_GNUC_PRINTF (3, 4);
G_END_DECLS

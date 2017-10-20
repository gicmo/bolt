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
 *
 * Error codes used inside Bolt.
 */
enum {
  BOLT_ERROR_FAILED = 0,
  BOLT_ERROR_UDEV,
} BoltError;


GQuark bolt_error_quark (void);
#define BOLT_ERROR (bolt_error_quark ())


G_END_DECLS

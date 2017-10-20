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

#include <gio/gio.h>

/**
 * SECTION:bolt-error
 * @Title: Error codes
 *
 */

static const GDBusErrorEntry bolt_error_entries[] = {
  {BOLT_ERROR_FAILED,     "org.freedesktop.Bolt.Error.Failed"},
  {BOLT_ERROR_UDEV,       "org.freedesktop.Bolt.Error.UDev"},
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

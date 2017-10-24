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

#define BOLT_DBUS_NAME "org.freedesktop.Bolt"
#define BOLT_DBUS_PATH "/org/freedesktop/Bolt"
#define BOLT_DBUS_INTERFACE "org.freedesktop.Bolt1.Manager"

#define BOLT_DBUS_DEVICE_INTERFACE "org.freedesktop.Bolt1.Device"


/**
 * BoltStatus
 * @BOLT_STATUS_DISCONNECTED: Device is not connected.
 * @BOLT_STATUS_CONNECTING: Device is currently connecting.
 * @BOLT_STATUS_CONNECTED: Device is connected, but not authorized.
 * @BOLT_STATUS_AUTHORIZING: Device is currently authorizing.
 * @BOLT_STATUS_AUTH_ERROR: Failed to authorize a device via a key.
 * @BOLT_STATUS_AUTHORIZED: Device connected and authorized.
 * @BOLT_STATUS_AUTHORIZED_SECURE: Device connected and securely authorized via a key.
 * @BOLT_STATUS_AUTHORIZED_NEWKEY: Device connected and authorized via a new key.
 * @BOLT_STATUS_GHOST: A parent device is authorizing and therefore the status
 *  of the device is not know, but most likely it will reappear after the parent
 *  device is done authenticating.
 *
 * The current status of the device.
 */
typedef enum {

  BOLT_STATUS_DISCONNECTED = 0,
  BOLT_STATUS_CONNECTING,
  BOLT_STATUS_CONNECTED,
  BOLT_STATUS_AUTHORIZING,
  BOLT_STATUS_AUTH_ERROR,
  BOLT_STATUS_AUTHORIZED,
  BOLT_STATUS_AUTHORIZED_SECURE,
  BOLT_STATUS_AUTHORIZED_NEWKEY,
  BOLT_STATUS_GHOST,

  BOLT_STATUS_LAST

} BoltStatus;

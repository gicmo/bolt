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

#pragma once

/* D-Bus API revision (here for the lack of a better place) */
#define BOLT_DBUS_API_VERSION 1U

/* logging */

#define BOLT_LOG_DEVICE_UID "BOLT_DEVICE_UID"
#define BOLT_LOG_DEVICE_NAME "BOLT_DEVICE_NAME"
#define BOLT_LOG_DEVICE_STATE "BOLT_DEVICE_STATE"

#define BOLT_LOG_ERROR_DOMAIN "ERROR_DOMAIN"
#define BOLT_LOG_ERROR_CODE "ERROR_CODE"
#define BOLT_LOG_ERROR_MESSAGE "ERROR_MESSAGE"

#define BOLT_LOG_TOPIC "BOLT_TOPIC"
#define BOLT_LOG_VERSION "BOLT_VERSION"
#define BOLT_LOG_CONTEXT "BOLT_LOG_CONTEXT"

/* logging - message ids */
#define BOLT_LOG_MSG_ID_STARTUP "dd11929c788e48bdbb6276fb5f26b08a"


/* dbus */

#define BOLT_DBUS_NAME "org.freedesktop.bolt"
#define BOLT_DBUS_PATH "/org/freedesktop/bolt"
#define BOLT_DBUS_INTERFACE "org.freedesktop.bolt1.Manager"

#define BOLT_DBUS_DEVICE_INTERFACE "org.freedesktop.bolt1.Device"

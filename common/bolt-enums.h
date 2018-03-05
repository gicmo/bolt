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

#include "bolt-names.h"
#include "bolt-enum-types.h"


gboolean          bolt_enum_validate (GType    enum_type,
                                      gint     value,
                                      GError **error);

gboolean          bolt_enum_class_validate (GEnumClass *enum_class,
                                            gint        value,
                                            GError    **error);

const char *      bolt_enum_to_string (GType    enum_type,
                                       gint     value,
                                       GError **error);

gint              bolt_enum_from_string (GType       enum_type,
                                         const char *string,
                                         GError    **error);

char *            bolt_flags_class_to_string (GFlagsClass *flags_class,
                                              guint        value,
                                              GError     **error);

gboolean          bolt_flags_class_from_string (GFlagsClass *flags_class,
                                                const char  *string,
                                                guint       *flags_out,
                                                GError     **error);

char *            bolt_flags_to_string (GType    flags_type,
                                        guint    value,
                                        GError **error);

gboolean          bolt_flags_from_string (GType       flags_type,
                                          const char *string,
                                          guint      *flags_out,
                                          GError    **error);

/**
 * BoltStatus
 * @BOLT_STATUS_UNKNOWN: Device is in an unknown state (should normally not happen).
 * @BOLT_STATUS_DISCONNECTED: Device is not connected.
 * @BOLT_STATUS_CONNECTED: Device is connected, but not authorized.
 * @BOLT_STATUS_AUTHORIZING: Device is currently authorizing.
 * @BOLT_STATUS_AUTH_ERROR: Failed to authorize a device via a key.
 * @BOLT_STATUS_AUTHORIZED: Device connected and authorized.
 * @BOLT_STATUS_AUTHORIZED_SECURE: Device connected and securely authorized via a key.
 * @BOLT_STATUS_AUTHORIZED_NEWKEY: Device connected and authorized via a new key.
 *
 * The current status of the device.
 */
typedef enum {

  BOLT_STATUS_UNKNOWN = -1,
  BOLT_STATUS_DISCONNECTED = 0,
  BOLT_STATUS_CONNECTED,
  BOLT_STATUS_AUTHORIZING,
  BOLT_STATUS_AUTH_ERROR,
  BOLT_STATUS_AUTHORIZED,
  BOLT_STATUS_AUTHORIZED_SECURE,
  BOLT_STATUS_AUTHORIZED_NEWKEY,

} BoltStatus;

const char *     bolt_status_to_string (BoltStatus status);
gboolean         bolt_status_is_authorized (BoltStatus status);
gboolean         bolt_status_is_connected (BoltStatus status);
gboolean         bolt_status_validate (BoltStatus status);

/**
 * BoltKeyState:
 * @BOLT_KEY_MISSING: no key
 * @BOLT_KEY_HAVE: key exists
 * @BOLT_KEY_NEW: key is new
 *
 * The state of the key.
 */

typedef enum {

  BOLT_KEY_MISSING = 0,
  BOLT_KEY_HAVE = 1,
  BOLT_KEY_NEW = 2

} BoltKeyState;

/**
 * BoltSecurity:
 * @BOLT_SECURITY_UNKNOWN : Unknown security.
 * @BOLT_SECURITY_NONE    : No security, all devices are automatically connected.
 * @BOLT_SECURITY_DPONLY  : Display Port only devices only.
 * @BOLT_SECURITY_USER    : User needs to authorize devices.
 * @BOLT_SECURITY_SECURE  : User needs to authorize devices. Authorization can
 *     be done via key exchange to verify the device identity.
 *
 * The security level of the thunderbolt domain.
 */
typedef enum {

  BOLT_SECURITY_NONE = 0,
  BOLT_SECURITY_DPONLY = 1,
  BOLT_SECURITY_USER = '1',
  BOLT_SECURITY_SECURE = '2',

  BOLT_SECURITY_LAST,
  BOLT_SECURITY_INVALID = BOLT_SECURITY_LAST

} BoltSecurity;


BoltSecurity     bolt_security_from_string (const char *str);
const char *     bolt_security_to_string (BoltSecurity security);
gboolean         bolt_security_validate (BoltSecurity security);

/**
 * BoltPolicy:
 * @BOLT_POLICY_DEFAULT: Default policy.
 * @BOLT_POLICY_MANUAL: Manual authorization of the device.
 * @BOLT_POLICY_AUTO: Connect the device automatically,
 *   with the best possible security level supported
 *   by the domain controller.
 *
 * What do to for connected devices.
 */
typedef enum {

  BOLT_POLICY_DEFAULT = 0,
  BOLT_POLICY_MANUAL = 1,
  BOLT_POLICY_AUTO = 2,

} BoltPolicy;


BoltPolicy       bolt_policy_from_string (const char *str);
const char *     bolt_policy_to_string (BoltPolicy policy);
gboolean         bolt_policy_validate (BoltPolicy policy);

/**
 * BoltAuthFlags:
 * @BOLT_AUTH_NONE: No flags set.
 *
 * Control authorization.
 */
typedef enum { /*< flags >*/

  BOLT_AUTH_NONE = 0

} BoltAuthFlags;

/**
 * BoltDeviceType:
 * @BOLT_DEVICE_HOST: The device representing the host
 * @BOLT_DEVICE_PERIPHERAL: A generic thunderbolt peripheral
 *
 * The type of the device.
 */
typedef enum {

  BOLT_DEVICE_HOST,
  BOLT_DEVICE_PERIPHERAL

} BoltDeviceType;

BoltDeviceType   bolt_device_type_from_string (const char *str);
const char *     bolt_device_type_to_string (BoltDeviceType type);
gboolean         bolt_device_type_validate (BoltDeviceType type);
gboolean         bolt_device_type_is_host (BoltDeviceType type);

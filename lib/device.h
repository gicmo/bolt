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


#ifndef __TB_DEVICE_H__
#define __TB_DEVICE_H__

#include <glib-object.h>


#include <gio/gio.h>

G_BEGIN_DECLS
#define TB_TYPE_DEVICE tb_device_get_type ()
G_DECLARE_FINAL_TYPE (TbDevice, tb_device, TB, DEVICE, GObject);

/**
 * TbAuth:
 * @TB_AUTH_UNKNOWN      : Current authorization not known
 * @TB_AUTH_UNAUTHORIZED : Device is not authorized
 * @TB_AUTH_AUTHORIZED   : Device is authorized and connected
 * @TB_AUTH_SECURED      : Device is authorized via key exchange

 * The current authorization status of the device.
 */
typedef enum {

  TB_AUTH_UNKNOWN = -1,
  TB_AUTH_UNAUTHORIZED = 0,
  TB_AUTH_AUTHORIZED = 1,
  TB_AUTH_SECURED = 2
} TbAuth;

/**
 * TbPolicy:
 * @TB_POLICY_UNKNOWN : Unknown policy (e.g. devices not in store).
 * @TB_POLICY_IGNORE  : Ignore the newly connected device.
 * @TB_POLICY_AUTO    : Automatically authorize the device system wide.
 *
 * What do to when a thunderbolt device is connected.
 */
typedef enum {
  TB_POLICY_UNKNOWN = -1,
  TB_POLICY_IGNORE = 0,
  TB_POLICY_AUTO = 1
} TbPolicy;

char *tb_policy_to_string (TbPolicy policy);
TbPolicy tb_policy_from_string (const char *str);

const char *tb_device_get_uid (const TbDevice *device);
const char *tb_device_get_name (const TbDevice *device);
guint tb_device_get_device_id (const TbDevice *device);
const char *tb_device_get_vendor_name (const TbDevice *device);
guint tb_device_get_vendor_id (const TbDevice *device);
const char *tb_device_get_sysfs_path (const TbDevice *device);
TbAuth tb_device_get_authorized (const TbDevice *device);
gboolean tb_device_in_store (const TbDevice *device);
TbPolicy tb_device_get_policy (const TbDevice *device);
GFile *tb_device_get_sysfs_keyfile (const TbDevice *device);

G_END_DECLS
#endif /* TB_DEVICE_H */

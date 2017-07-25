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
#include <gudev/gudev.h>

#include <gio/gio.h>

G_BEGIN_DECLS
#define TB_TYPE_DEVICE tb_device_get_type ()
G_DECLARE_FINAL_TYPE (TbDevice, tb_device, TB, DEVICE, GObject);

struct _TbDevice
{
  GObject object;

  /* db or udev */
  char *uid;

  guint vendor;
  char *vendor_name;

  guint device;
  char *device_name;

  /* current status (udev) */
  char *sysfs;
  gint  authorized;

  /* db */
  GFile   *db;
  GFile   *key;

  gboolean autoconnect;
};

gboolean tb_device_update_from_udev (TbDevice     *device,
                                     GUdevDevice * udev);

const char *tb_device_get_uid (const TbDevice *device);
const char *tb_device_get_name (const TbDevice *device);
const char *tb_device_get_vendor_name (const TbDevice *device);
const char *tb_device_get_sysfs_path (const TbDevice *device);
gint tb_device_get_authorized (const TbDevice *device);
gboolean tb_device_in_store (const TbDevice *device);
gboolean tb_device_autoconnect (const TbDevice *device);
GFile *tb_device_get_key (const TbDevice *device);
gboolean tb_device_have_key (const TbDevice *device);
GFile *tb_device_get_sysfs_keyfile (const TbDevice *device);

G_END_DECLS
#endif /* TB_DEVICE_H */

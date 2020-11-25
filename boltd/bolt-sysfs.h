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

#include "bolt-enums.h"

#include "bolt-wire.h"

#include <glib.h>

struct udev;
struct udev_device;

G_BEGIN_DECLS

/* Device identification */
typedef struct _BoltIdent BoltIdent;
struct _BoltIdent
{
  struct udev_device *udev;

  const char         *name;
  const char         *vendor;
};

#define BOLT_IDENT_INIT {NULL, NULL, NULL}

void                 bolt_ident_clear (BoltIdent *id);

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (BoltIdent, bolt_ident_clear);

const char *         bolt_sysfs_device_get_unique_id (struct udev_device *dev,
                                                      GError            **error);

typedef enum BoltStatTime {
  BOLT_ST_ATIME,
  BOLT_ST_CTIME,
  BOLT_ST_MTIME
} BoltStatTime;

gint64               bolt_sysfs_device_get_time (struct udev_device *udev,
                                                 BoltStatTime        st);

gboolean             bolt_sysfs_device_is_domain (struct udev_device *udev,
                                                  GError            **error);

struct udev_device * bolt_sysfs_domain_for_device (struct udev_device  *udev,
                                                   struct udev_device **host);

BoltSecurity         bolt_sysfs_security_for_device (struct udev_device *udev,
                                                     GError            **error);

gboolean             bolt_sysfs_device_ident (struct udev_device *udev,
                                              BoltIdent          *id,
                                              GError            **error);

gboolean             bolt_sysfs_host_ident (struct udev_device *udev,
                                            BoltIdent          *id,
                                            GError            **error);

int                  bolt_sysfs_count_hosts (struct udev *udev,
                                             GError     **error);

gboolean             bolt_sysfs_nhi_id_for_domain (struct udev_device *udev,
                                                   guint32            *id,
                                                   GError            **error);

typedef struct _BoltDevInfo
{

  /* always included  */
  gint   authorized;
  gssize keysize;
  gint   boot;

  /* if 'full' is true the rest is valid */
  gboolean    full;
  gint64      ctim;
  const char *syspath;
  const char *parent;       /* the uid */
  guint       generation;

  /* link speed, may be 0 if unknown */
  BoltLinkSpeed linkspeed;

} BoltDevInfo;

gboolean             bolt_sysfs_info_for_device (struct udev_device *udev,
                                                 gboolean            full,
                                                 BoltDevInfo        *info,
                                                 GError            **error);

void                 bolt_sysfs_read_link_speed (struct udev_device *udev,
                                                 BoltLinkSpeed      *speed);

gboolean             bolt_sysfs_read_boot_acl (struct udev_device *udev,
                                               GStrv              *out,
                                               GError            **error);

gboolean             bolt_sysfs_write_boot_acl (const char *device,
                                                GStrv       acl,
                                                GError    **error);

gboolean             bolt_sysfs_read_iommu (struct udev_device *udev,
                                            gboolean           *out,
                                            GError            **error);

/* NHI id related functions*/
gboolean             bolt_nhi_uuid_is_stable (guint32   pci_id,
                                              gboolean *stability,
                                              GError  **error);

G_END_DECLS

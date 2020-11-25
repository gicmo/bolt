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

#include <gio/gio.h>

#include "bolt-enums.h"
#include "bolt-wire.h"


G_BEGIN_DECLS

typedef struct MockDevId
{

  gint        vendor_id;
  const char *vendor_name;

  gint        device_id;
  const char *device_name;

  const char *unique_id;

} MockDevId;

#define MOCK_TYPE_SYSFS mock_sysfs_get_type ()
G_DECLARE_FINAL_TYPE (MockSysfs, mock_sysfs, MOCK, SYSFS, GObject);

MockSysfs *      mock_sysfs_new (void);

const char *     mock_sysfs_force_power_add (MockSysfs *ms);

gboolean         mock_sysfs_force_power_remove (MockSysfs *ms);

void             mock_sysfs_force_power_load (MockSysfs *ms);

void             mock_sysfs_force_power_unload (MockSysfs *ms);

char *           mock_sysfs_force_power_read (MockSysfs *ms);

gboolean         mock_sysfs_force_power_enabled (MockSysfs *ms);

const char *     mock_sysfs_dmi_id_add (MockSysfs  *ms,
                                        const char *sys_vendor,
                                        const char *product_name,
                                        const char *product_version);

gboolean         mock_sysfs_dmi_id_remove (MockSysfs *ms);

const char *     mock_sysfs_domain_add (MockSysfs   *ms,
                                        BoltSecurity security,
                                        ...) G_GNUC_NULL_TERMINATED;

const char *     mock_sysfs_domain_get_syspath (MockSysfs  *ms,
                                                const char *id);

gboolean         mock_sysfs_domain_remove (MockSysfs  *ms,
                                           const char *id);

GStrv            mock_sysfs_domain_bootacl_get (MockSysfs  *ms,
                                                const char *id,
                                                GError    **error);

gboolean         mock_sysfs_domain_bootacl_set (MockSysfs  *ms,
                                                const char *id,
                                                GStrv       acl,
                                                GError    **error);

gboolean         mock_syfs_domain_iommu_set (MockSysfs  *ms,
                                             const char *id,
                                             const char *val,
                                             GError    **error);

const char *     mock_sysfs_host_add (MockSysfs  *ms,
                                      const char *domain,
                                      MockDevId  *id);

void             mock_sysfs_host_remove (MockSysfs  *ms,
                                         const char *host);

const char *     mock_sysfs_device_add (MockSysfs     *ms,
                                        const char    *parent,
                                        MockDevId     *id,
                                        guint          authorized,
                                        const char    *key,
                                        gint           boot,
                                        BoltLinkSpeed *link);

const char *     mock_sysfs_device_get_syspath (MockSysfs  *ms,
                                                const char *id);

const char *     mock_sysfs_device_get_parent (MockSysfs  *ms,
                                               const char *id);

gboolean         mock_sysfs_device_remove (MockSysfs  *ms,
                                           const char *id);

gboolean         mock_sysfs_set_osrelease (MockSysfs  *ms,
                                           const char *version);


G_END_DECLS

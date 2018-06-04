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


G_BEGIN_DECLS

#define MOCK_TYPE_SYSFS mock_sysfs_get_type ()
G_DECLARE_FINAL_TYPE (MockSysfs, mock_sysfs, MOCK, SYSFS, GObject);

MockSysfs *      mock_sysfs_new (void);

const char *     mock_sysfs_domain_add (MockSysfs   *ms,
                                        BoltSecurity security);

const char *     mock_sysfs_domain_get_syspath (MockSysfs  *ms,
                                                const char *id);

G_END_DECLS

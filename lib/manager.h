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


#ifndef __TB_MANAGER_H__
#define __TB_MANAGER_H__

#include <glib-object.h>

#include "device.h"

G_BEGIN_DECLS
#define TB_TYPE_MANAGER tb_manager_get_type ()
G_DECLARE_FINAL_TYPE (TbManager, tb_manager, TB, MANAGER, GObject);

/**
 * TbSecurity:
 * @TB_SECURITY_UNKNOWN : Unknown security.
 * @TB_SECURITY_NONE    : No security, all devices are automatically connected.
 * @TB_SECURITY_DPONLY  : Display Port only devices only.
 * @TB_SECURITY_USER    : User needs to authorize devices.
 * @TB_SECURITY_SECURE  : User needs to authorize devices. Authorization will
 *     be done via key exchange to verify the device identity.
 *
 * The security level of the thunderbolt domain.
 */
typedef enum {

  TB_SECURITY_UNKNOWN = -1,
  TB_SECURITY_NONE = 0,
  TB_SECURITY_DPONLY = 1,
  TB_SECURITY_USER = '1',
  TB_SECURITY_SECURE = '2'
} TbSecurity;

GQuark tb_error_quark (void);
#define TB_ERROR (tb_error_quark ())

TbSecurity       tb_security_from_string (const char *str);
char            *tb_security_to_string (TbSecurity security);

TbManager       *tb_manager_new (GError **error);

const GPtrArray *tb_manager_list_attached (TbManager *mgr);
TbDevice        *tb_manager_lookup (TbManager  *mgr,
                                    const char *uid);

gboolean         tb_manager_device_stored (TbManager *mgr,
                                           TbDevice  *dev);

gboolean         tb_manager_store (TbManager *mgr,
                                   TbDevice  *device,
                                   GError   **error);

gboolean         tb_manager_have_key (TbManager *mgr,
                                      TbDevice  *dev);

int              tb_manager_ensure_key (TbManager *mgr,
                                        TbDevice  *dev,
                                        gboolean   replace,
                                        gboolean  *created,
                                        GError   **error);

TbSecurity       tb_manager_get_security (TbManager *mgr);

gboolean         tb_manager_authorize (TbManager *mgr,
                                       TbDevice  *dev,
                                       GError   **error);

G_END_DECLS
#endif /* TB_MANAGER_H */

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
#include "bolt-device.h"
#include "bolt-proxy.h"

G_BEGIN_DECLS

#define BOLT_TYPE_CLIENT bolt_client_get_type ()
G_DECLARE_FINAL_TYPE (BoltClient, bolt_client, BOLT, CLIENT, BoltProxy);

BoltClient *    bolt_client_new (GError **error);
GPtrArray *     bolt_client_list_devices (BoltClient *client,
                                          GError    **error);
BoltDevice *    bolt_client_get_device (BoltClient *client,
                                        const char *uid,
                                        GError    **error);
BoltDevice *    bolt_client_enroll_device (BoltClient *client,
                                           const char *uid,
                                           BoltPolicy  policy,
                                           GError    **error);
gboolean        bolt_client_forget_device (BoltClient *client,
                                           const char *uid,
                                           GError    **error);
G_END_DECLS

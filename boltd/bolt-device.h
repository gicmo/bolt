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

#include "bolt-auth.h"
#include "bolt-enums.h"
#include "bolt-exported.h"

/* forward declaration */
struct udev_device;
typedef struct _BoltDomain BoltDomain;

G_BEGIN_DECLS

#define BOLT_TYPE_DEVICE bolt_device_get_type ()
G_DECLARE_FINAL_TYPE (BoltDevice, bolt_device, BOLT, DEVICE, BoltExported);

BoltDevice *      bolt_device_new_for_udev (struct udev_device *udev,
                                            BoltDomain         *domain,
                                            GError            **error);

const char *      bolt_device_export (BoltDevice      *device,
                                      GDBusConnection *connection,
                                      GError         **error);

void              bolt_device_unexport (BoltDevice *device);

BoltStatus        bolt_device_connected (BoltDevice         *dev,
                                         BoltDomain         *domain,
                                         struct udev_device *udev);

BoltStatus        bolt_device_disconnected (BoltDevice *dev);

gboolean          bolt_device_is_connected (BoltDevice *device);

gboolean          bolt_device_is_authorized (BoltDevice *device);

BoltStatus        bolt_device_update_from_udev (BoltDevice         *dev,
                                                struct udev_device *udev);

void              bolt_device_authorize (BoltDevice         *dev,
                                         BoltAuth           *auth,
                                         GAsyncReadyCallback callback,
                                         gpointer            user_data);

void              bolt_device_authorize_idle (BoltDevice         *dev,
                                              BoltAuth           *auth,
                                              GAsyncReadyCallback callback,
                                              gpointer            user_data);

BoltDomain *      bolt_device_get_domain (BoltDevice *dev);

BoltKeyState      bolt_device_get_keystate (BoltDevice *dev);

const char *      bolt_device_get_name (BoltDevice *dev);

const char *      bolt_device_get_object_path (BoltDevice *device);

BoltPolicy        bolt_device_get_policy (BoltDevice *dev);

const char *      bolt_device_get_uid (BoltDevice *dev);

BoltSecurity      bolt_device_get_security (BoltDevice *dev);

gboolean          bolt_device_get_stored (BoltDevice *dev);

BoltStatus        bolt_device_get_status (BoltDevice *dev);

BoltAuthFlags     bolt_device_get_authflags (BoltDevice *dev);

const char *      bolt_device_get_syspath (BoltDevice *dev);

const char *      bolt_device_get_vendor (BoltDevice *dev);

BoltDeviceType    bolt_device_get_device_type (BoltDevice *dev);

gboolean          bolt_device_is_host (BoltDevice *dev);

const char *      bolt_device_get_label (BoltDevice *dev);

guint64           bolt_device_get_authtime (BoltDevice *dev);

guint64           bolt_device_get_conntime (BoltDevice *dev);

guint64           bolt_device_get_storetime (BoltDevice *dev);

guint             bolt_device_get_generation (BoltDevice *dev);

gboolean          bolt_device_has_iommu (BoltDevice *dev);

gboolean          bolt_device_has_key (BoltDevice *dev);

gboolean          bolt_device_supports_secure_mode (BoltDevice *dev);

gboolean          bolt_device_check_authflag (BoltDevice   *dev,
                                              BoltAuthFlags flag);

gboolean          bolt_device_get_key_from_sysfs (BoltDevice *dev,
                                                  BoltKey   **key,
                                                  GError    **error);
gboolean          bolt_device_load_key (BoltDevice *dev,
                                        BoltKey   **key,
                                        GError    **error);

/* bolt_domain_foreach helpers */
void              bolt_bootacl_add (gpointer domain,
                                    gpointer device);

void              bolt_bootacl_del (gpointer domain,
                                    gpointer device);

G_END_DECLS

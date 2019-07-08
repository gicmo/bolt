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

#include "config.h"

#include "bolt-domain.h"

#include <gio/gio.h>

struct _BoltDomain
{
  BoltProxy parent;
};

enum {
  PROP_0,

  /* D-Bus Props */
  PROP_UID,
  PROP_ID,
  PROP_SYSPATH,
  PROP_SECURITY,
  PROP_BOOTACL,
  PROP_IOMMU,

  PROP_LAST
};

static GParamSpec *props[PROP_LAST] = {NULL, };

G_DEFINE_TYPE (BoltDomain,
               bolt_domain,
               BOLT_TYPE_PROXY);


static void
bolt_domain_class_init (BoltDomainClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = bolt_proxy_property_getter;
  gobject_class->set_property = bolt_proxy_property_setter;

  props[PROP_UID] =
    g_param_spec_string ("uid", "Uid",
                         "The unique identifier.",
                         "unknown",
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_ID] =
    g_param_spec_string ("id", "Id",
                         "The sysfs name.",
                         "unknown",
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_SYSPATH] =
    g_param_spec_string ("syspath", "SysfsPath",
                         "Sysfs path of the udev device.",
                         "unknown",
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_SECURITY]
    = g_param_spec_enum ("security", "SecurityLevel",
                         "The security level set in the BIOS.",
                         BOLT_TYPE_SECURITY,
                         BOLT_SECURITY_UNKNOWN,
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_BOOTACL] =
    g_param_spec_boxed ("bootacl", "BootACL",
                        "Pre-boot access control list (uuids).",
                        G_TYPE_STRV,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS);

  props[PROP_IOMMU] =
    g_param_spec_boolean ("iommu", "IOMMU",
                          "Is IOMMU based DMA protection active?",
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     props);

}

static void
bolt_domain_init (BoltDomain *domain)
{
}

/* public methods */
BoltDomain *
bolt_domain_new_for_object_path (GDBusConnection *bus,
                                 const char      *path,
                                 GCancellable    *cancel,
                                 GError         **error)
{
  BoltDomain *dom;

  g_return_val_if_fail (G_IS_DBUS_CONNECTION (bus), NULL);
  g_return_val_if_fail (path != NULL, NULL);
  g_return_val_if_fail (!cancel || G_IS_CANCELLABLE (cancel), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  dom = g_initable_new (BOLT_TYPE_DOMAIN,
                        cancel, error,
                        "g-flags", G_DBUS_PROXY_FLAGS_NONE,
                        "g-connection", bus,
                        "g-name", BOLT_DBUS_NAME,
                        "g-object-path", path,
                        "g-interface-name", BOLT_DBUS_DOMAIN_INTERFACE,
                        NULL);

  return dom;
}

const char *
bolt_domain_get_uid (BoltDomain *domain)
{
  const char *str;

  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), NULL);

  str = bolt_proxy_get_string_by_pspec (domain, props[PROP_UID]);

  return str;
}

const char *
bolt_domain_get_id (BoltDomain *domain)
{
  const char *str;

  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), NULL);

  str = bolt_proxy_get_string_by_pspec (domain, props[PROP_ID]);

  return str;
}

const char *
bolt_domain_get_syspath (BoltDomain *domain)
{
  const char *str;

  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), NULL);

  str = bolt_proxy_get_string_by_pspec (domain, props[PROP_SYSPATH]);

  return str;
}

BoltSecurity
bolt_domain_get_security (BoltDomain *domain)
{
  gint val = BOLT_SECURITY_UNKNOWN;

  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), val);

  val = bolt_proxy_get_enum_by_pspec (domain, props[PROP_SECURITY]);

  return val;
}

char **
bolt_domain_get_bootacl (BoltDomain *domain)
{
  char **strv;

  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), NULL);

  strv = bolt_proxy_get_strv_by_pspec (domain, props[PROP_BOOTACL]);

  return strv;
}

gboolean
bolt_domain_is_online (BoltDomain *domain)
{
  const char *syspath;

  syspath = bolt_domain_get_syspath (domain);

  return syspath != NULL;
}

gboolean
bolt_domain_has_iommu (BoltDomain *domain)
{
  gboolean val = FALSE;

  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), val);

  val = bolt_proxy_get_bool_by_pspec (domain, props[PROP_IOMMU]);

  return val;
}

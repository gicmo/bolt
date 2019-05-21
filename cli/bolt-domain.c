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

  props[PROP_UID] =
    g_param_spec_string ("uid",
                         "Uid", NULL,
                         "unknown",
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_NICK);

  props[PROP_ID] =
    g_param_spec_string ("id",
                         "Id", NULL,
                         "unknown",
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_NICK);

  props[PROP_SYSPATH] =
    g_param_spec_string ("syspath",
                         "SysfsPath", NULL,
                         "unknown",
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_NICK);

  props[PROP_SECURITY]
    = g_param_spec_enum ("security",
                         "SecurityLevel", NULL,
                         BOLT_TYPE_SECURITY,
                         BOLT_SECURITY_UNKNOWN,
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_NAME);

  props[PROP_BOOTACL] =
    g_param_spec_boxed ("bootacl",
                        "BootACL", NULL,
                        G_TYPE_STRV,
                        G_PARAM_READABLE |
                        G_PARAM_STATIC_NAME);

  props[PROP_IOMMU] =
    g_param_spec_boolean ("iommu",
                          "IOMMU", NULL,
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
  const char *key;
  const char *str;

  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), NULL);

  key = g_param_spec_get_name (props[PROP_UID]);
  str = bolt_proxy_get_property_string (BOLT_PROXY (domain), key);

  return str;
}

const char *
bolt_domain_get_id (BoltDomain *domain)
{
  const char *key;
  const char *str;

  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), NULL);

  key = g_param_spec_get_name (props[PROP_ID]);
  str = bolt_proxy_get_property_string (BOLT_PROXY (domain), key);

  return str;
}

const char *
bolt_domain_get_syspath (BoltDomain *domain)
{
  const char *key;
  const char *str;

  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), NULL);

  key = g_param_spec_get_name (props[PROP_SYSPATH]);
  str = bolt_proxy_get_property_string (BOLT_PROXY (domain), key);

  return str;
}

BoltSecurity
bolt_domain_get_security (BoltDomain *domain)
{
  const char *key;
  gboolean ok;
  gint val = BOLT_SECURITY_UNKNOWN;

  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), val);

  key = g_param_spec_get_name (props[PROP_SECURITY]);
  ok = bolt_proxy_get_property_enum (BOLT_PROXY (domain), key, &val);

  if (!ok)
    g_warning ("failed to get enum property '%s'", key);

  return val;
}

char **
bolt_domain_get_bootacl (BoltDomain *domain)
{
  const char *key;
  char **strv;

  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), NULL);

  key = g_param_spec_get_name (props[PROP_BOOTACL]);
  strv = bolt_proxy_get_property_strv (BOLT_PROXY (domain), key);

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
  const char *key;
  gboolean val = FALSE;
  gboolean ok;

  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), FALSE);

  key = g_param_spec_get_name (props[PROP_IOMMU]);
  ok = bolt_proxy_get_property_bool (BOLT_PROXY (domain), key, &val);

  if (!ok)
    g_warning ("failed to get bool property '%s'", key);

  return val;
}

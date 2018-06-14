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
  PROP_ID,
  PROP_SYSPATH,
  PROP_SECURITY,

  PROP_LAST
};

static GParamSpec *props[PROP_LAST] = {NULL, };

G_DEFINE_TYPE (BoltDomain,
               bolt_domain,
               BOLT_TYPE_PROXY);

static void
bolt_domain_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  if (bolt_proxy_get_dbus_property (object, pspec, value))
    return;
}



static void
bolt_domain_class_init (BoltDomainClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = bolt_domain_get_property;

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

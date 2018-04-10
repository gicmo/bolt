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

#include "config.h"

#include "bolt-auth.h"
#include "bolt-device.h"
#include "bolt-io.h"
#include "bolt-log.h"
#include "bolt-store.h"

#include <gio/gio.h>

static void     async_result_iface_init (GAsyncResultIface *iface);

struct _BoltAuth
{
  GObject      object;

  GObject     *origin;
  BoltSecurity level;
  BoltKey     *key;

  /* the device  */
  BoltDevice *dev;

  /* result */
  GError *error;
};


enum {
  PROP_0,

  PROP_ORIGIN,
  PROP_LEVEL,
  PROP_KEY,

  PROP_DEVICE,
  PROP_ERROR,

  PROP_LAST
};

static GParamSpec *props[PROP_LAST] = { NULL, };

G_DEFINE_TYPE_WITH_CODE (BoltAuth, bolt_auth, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_RESULT, async_result_iface_init));


static void
bolt_auth_finalize (GObject *object)
{
  BoltAuth *auth = BOLT_AUTH (object);

  g_clear_object (&auth->origin);
  g_clear_object (&auth->key);
  g_clear_object (&auth->dev);
  g_clear_error (&auth->error);

  G_OBJECT_CLASS (bolt_auth_parent_class)->finalize (object);
}

static void
bolt_auth_init (BoltAuth *auth)
{
}

static void
bolt_auth_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
  BoltAuth *auth = BOLT_AUTH (object);

  switch (prop_id)
    {

    case PROP_ORIGIN:
      g_value_set_object (value, auth->origin);
      break;

    case PROP_LEVEL:
      g_value_set_enum (value, auth->level);
      break;

    case PROP_KEY:
      g_value_set_object (value, auth->key);
      break;

    case PROP_DEVICE:
      g_value_set_object (value, auth->dev);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bolt_auth_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
  BoltAuth *auth = BOLT_AUTH (object);

  switch (prop_id)
    {
    case PROP_ORIGIN:
      auth->origin = g_value_dup_object (value);
      break;

    case PROP_LEVEL:
      auth->level = g_value_get_enum (value);
      break;

    case PROP_KEY:
      auth->key = g_value_dup_object (value);
      break;

    case PROP_DEVICE:
      g_return_if_fail (auth->dev == NULL);
      auth->dev = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bolt_auth_class_init (BoltAuthClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = bolt_auth_finalize;

  gobject_class->get_property = bolt_auth_get_property;
  gobject_class->set_property = bolt_auth_set_property;

  props[PROP_ORIGIN] =
    g_param_spec_object ("origin",
                         NULL, NULL,
                         G_TYPE_OBJECT,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_NICK);

  props[PROP_LEVEL] =
    g_param_spec_enum ("level",
                       NULL, NULL,
                       BOLT_TYPE_SECURITY,
                       BOLT_SECURITY_NONE,
                       G_PARAM_READWRITE |
                       G_PARAM_CONSTRUCT_ONLY |
                       G_PARAM_STATIC_NICK);

  props[PROP_KEY] =
    g_param_spec_object ("key",
                         NULL, NULL,
                         BOLT_TYPE_KEY,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_NICK);

  props[PROP_DEVICE] =
    g_param_spec_object ("device",
                         NULL, NULL,
                         BOLT_TYPE_DEVICE,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_NICK);

  props[PROP_ERROR] =
    g_param_spec_boxed ("error", NULL, NULL,
                        G_TYPE_ERROR,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_NICK);

  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     props);
}

/* async result methods */
static gpointer
async_result_get_user_data (GAsyncResult *res)
{
  return NULL;
}

static GObject *
async_result_get_source_object (GAsyncResult *res)
{
  return G_OBJECT (BOLT_AUTH (res)->dev);
}

static gboolean
async_result_is_tagged (GAsyncResult *res,
                        gpointer      source_tag)
{
  return FALSE;
}

static void
async_result_iface_init (GAsyncResultIface *iface)
{
  iface->get_source_object = async_result_get_source_object;
  iface->get_user_data = async_result_get_user_data;
  iface->is_tagged = async_result_is_tagged;
}


/* public methods */

BoltAuth *
bolt_auth_new (gpointer     origin,
               BoltSecurity level,
               BoltKey     *key)
{
  BoltAuth *auth;

  auth = g_object_new (BOLT_TYPE_AUTH,
                       "origin", origin,
                       "level", level,
                       "key", key,
                       NULL);

  return auth;
}

void
bolt_auth_return_new_error (BoltAuth   *auth,
                            GQuark      domain,
                            gint        code,
                            const char *format,
                            ...)
{
  va_list args;

  va_start (args, format);
  auth->error = g_error_new_valist (domain, code, format, args);
  va_end (args);
}

void
bolt_auth_return_error (BoltAuth *auth,
                        GError  **error)
{
  g_return_if_fail (error != NULL && *error != NULL);
  g_return_if_fail (auth->error == NULL);

  auth->error = g_steal_pointer (error);
}

gboolean
bolt_auth_check (BoltAuth *auth,
                 GError  **error)
{

  if (auth->error)
    {
      g_autoptr(GError) err = g_error_copy (auth->error);
      g_propagate_error (error, g_steal_pointer (&err));
      return FALSE;
    }

  return TRUE;
}


BoltSecurity
bolt_auth_get_level (BoltAuth *auth)
{
  return auth->level;
}

BoltKey *
bolt_auth_get_key (BoltAuth *auth)
{
  return auth->key;
}

gpointer
bolt_auth_get_origin (BoltAuth *auth)
{
  return auth->origin;
}

BoltStatus
bolt_auth_to_status (BoltAuth *auth)
{
  g_return_val_if_fail (BOLT_IS_AUTH (auth), BOLT_STATUS_UNKNOWN);

  if (auth->error != NULL)
    return BOLT_STATUS_AUTH_ERROR;

  switch (auth->level)
    {
    case BOLT_SECURITY_SECURE:
    case BOLT_SECURITY_USER:
      return BOLT_STATUS_AUTHORIZED;

    case BOLT_SECURITY_DPONLY:
    case BOLT_SECURITY_USBONLY:
    case BOLT_SECURITY_NONE:
      bolt_bug ("unexpected security in BoltAuth::level: %s",
                bolt_security_to_string (auth->level));
      return BOLT_STATUS_AUTHORIZED;

    case BOLT_SECURITY_UNKNOWN:
      bolt_bug ("unknown status in BoltAUth::level");
      return BOLT_STATUS_UNKNOWN;
    }

  return BOLT_STATUS_UNKNOWN;
}

BoltAuthFlags
bolt_auth_to_flags (BoltAuth      *auth,
                    BoltAuthFlags *mask)
{
  BoltKeyState ks;

  g_return_val_if_fail (BOLT_IS_AUTH (auth), 0);

  if (mask)
    *mask = 0;

  if (auth->error != NULL)
    return 0;

  if (auth->level != BOLT_SECURITY_SECURE)
    return 0;

  if (mask)
    *mask = BOLT_AUTH_SECURE;

  ks = bolt_key_get_state (auth->key);

  if (ks == BOLT_KEY_NEW)
    return 0;

  return BOLT_AUTH_SECURE;
}

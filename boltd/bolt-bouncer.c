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

#include "bolt-bouncer.h"

#include "bolt-log.h"
#include "bolt-str.h"

#include "bolt-exported.h"

#include <gio/gio.h>
#include <polkit/polkit.h>

static void     bouncer_initable_iface_init (GInitableIface *iface);


static gboolean bouncer_initialize (GInitable    *initable,
                                    GCancellable *cancellable,
                                    GError      **error);

#ifndef HAVE_POLKIT_AUTOPTR
G_DEFINE_AUTOPTR_CLEANUP_FUNC (PolkitAuthorizationResult, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (PolkitDetails, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (PolkitSubject, g_object_unref)
#endif

struct _BoltBouncer
{
  GObject object;

  /* */
  PolkitAuthority *authority;
};

G_DEFINE_TYPE_WITH_CODE (BoltBouncer, bolt_bouncer, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                bouncer_initable_iface_init));


static void
bolt_bouncer_finalize (GObject *object)
{
  BoltBouncer *bouncer = BOLT_BOUNCER (object);

  g_clear_object (&bouncer->authority);

  G_OBJECT_CLASS (bolt_bouncer_parent_class)->finalize (object);
}

static void
bolt_bouncer_init (BoltBouncer *bouncer)
{
}

static void
bolt_bouncer_class_init (BoltBouncerClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = bolt_bouncer_finalize;

}

static void
bouncer_initable_iface_init (GInitableIface *iface)
{
  iface->init = bouncer_initialize;
}

static gboolean
bouncer_initialize (GInitable    *initable,
                    GCancellable *cancellable,
                    GError      **error)
{
  BoltBouncer *bnc = BOLT_BOUNCER (initable);

  bolt_info (LOG_TOPIC ("bouncer"), "initializing polkit");
  bnc->authority = polkit_authority_get_sync (cancellable, error);

  return bnc->authority != NULL;
}

/* internal methods */

static gboolean
bolt_bouncer_check_action (BoltBouncer           *bnc,
                           GDBusMethodInvocation *inv,
                           const char            *action,
                           gboolean              *authorized,
                           GError               **error)
{
  g_autoptr(PolkitSubject) subject = NULL;
  g_autoptr(PolkitDetails) details = NULL;
  g_autoptr(PolkitAuthorizationResult) res = NULL;
  PolkitCheckAuthorizationFlags flags;
  const char *sender;

  sender = g_dbus_method_invocation_get_sender (inv);

  subject = polkit_system_bus_name_new (sender);
  details = polkit_details_new ();

  flags = POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION;
  res = polkit_authority_check_authorization_sync (bnc->authority,
                                                   subject,
                                                   action, details,
                                                   flags,
                                                   NULL, error);
  if (res == NULL)
    return FALSE;

  *authorized = polkit_authorization_result_get_is_authorized (res);
  return TRUE;
}

static gboolean
handle_authorize_method (BoltExported          *exported,
                         GDBusMethodInvocation *inv,
                         GError               **error,
                         gpointer               user_data)
{
  g_autoptr(PolkitSubject) subject = NULL;
  g_autoptr(PolkitDetails) details = NULL;
  gboolean authorized = FALSE;
  BoltBouncer *bnc;
  const char *method_name;
  const char *sender;
  const char *action;

  bnc = BOLT_BOUNCER (user_data);
  method_name = g_dbus_method_invocation_get_method_name (inv);
  sender = g_dbus_method_invocation_get_sender (inv);

  subject = polkit_system_bus_name_new (sender);
  details = polkit_details_new ();

  action = NULL;

  if (bolt_streq (method_name, "EnrollDevice"))
    action = "org.freedesktop.bolt.enroll";
  else if (bolt_streq (method_name, "Authorize"))
    action = "org.freedesktop.bolt.authorize";
  else if (bolt_streq (method_name, "ForgetDevice"))
    action = "org.freedesktop.bolt.manage";
  else if (bolt_streq (method_name, "ForcePower"))
    action = "org.freedesktop.bolt.manage";
  else if (bolt_streq (method_name, "ListDomains"))
    authorized = TRUE;
  else if (bolt_streq (method_name, "DomainById"))
    authorized = TRUE;
  else if (bolt_streq (method_name, "ListDevices"))
    authorized = TRUE;
  else if (bolt_streq (method_name, "DeviceByUid"))
    authorized = TRUE;
  else if (bolt_streq (method_name, "ListGuards"))
    authorized = TRUE;

  if (!authorized && action)
    {
      PolkitCheckAuthorizationFlags flags;
      g_autoptr(PolkitAuthorizationResult) res = NULL;

      flags = POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION;
      res = polkit_authority_check_authorization_sync (bnc->authority,
                                                       subject,
                                                       action, details,
                                                       flags,
                                                       NULL, error);
      if (res == NULL)
        return FALSE;

      authorized = polkit_authorization_result_get_is_authorized (res);
    }

  if (authorized == FALSE)
    g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED,
                 "Bolt operation '%s' not allowed for user",
                 method_name);

  return authorized;
}

static gboolean
handle_authorize_property (BoltExported          *exported,
                           const char            *name,
                           gboolean               setting,
                           GDBusMethodInvocation *inv,
                           GError               **error,
                           gpointer               user_data)
{
  const char *type_name = G_OBJECT_TYPE_NAME (exported);
  const char *action = NULL;
  gboolean authorized = FALSE;
  BoltBouncer *bnc;

  bnc = BOLT_BOUNCER (user_data);

  if (bolt_streq (type_name, "BoltDevice"))
    {
      if (bolt_streq (name, "label"))
        action = "org.freedesktop.bolt.manage";
      else if (bolt_streq (name, "policy"))
        action = "org.freedesktop.bolt.manage";
    }
  else if (bolt_streq (type_name, "BoltDomain"))
    {
      if (bolt_streq (name, "bootacl"))
        action = "org.freedesktop.bolt.manage";
    }
  else if (bolt_streq (type_name, "BoltManager"))
    {
      if (bolt_streq (name, "auth-mode"))
        action = "org.freedesktop.bolt.manage";
    }

  if (!authorized && action)
    {
      gboolean ok;
      ok = bolt_bouncer_check_action (bnc,
                                      inv,
                                      action,
                                      &authorized,
                                      error);
      if (!ok)
        return FALSE;
    }


  if (authorized == FALSE)
    g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED,
                 "Setting property of '%s.%s' not allowed for user",
                 type_name, name);

  return authorized;
}

/* public methods */
BoltBouncer *
bolt_bouncer_new (GCancellable *cancellable,
                  GError      **error)
{
  return g_initable_new (BOLT_TYPE_BOUNCER,
                         cancellable, error,
                         NULL);
}

void
bolt_bouncer_add_client (BoltBouncer *bnc,
                         gpointer     client)
{

  g_return_if_fail (BOLT_IS_BOUNCER (bnc));
  g_return_if_fail (BOLT_IS_EXPORTED (client));

  g_signal_connect_object (client, "authorize-method",
                           G_CALLBACK (handle_authorize_method),
                           bnc, 0);

  g_signal_connect_object (client, "authorize-property",
                           G_CALLBACK (handle_authorize_property),
                           bnc, 0);
}

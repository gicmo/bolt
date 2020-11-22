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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
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
#include "bolt-config.h"
#include "bolt-device.h"
#include "bolt-domain.h"
#include "bolt-error.h"
#include "bolt-log.h"
#include "bolt-power.h"
#include "bolt-store.h"
#include "bolt-str.h"
#include "bolt-sysfs.h"
#include "bolt-time.h"
#include "bolt-udev.h"
#include "bolt-unix.h"
#include "bolt-watchdog.h"

#include "bolt-manager.h"

#include <libudev.h>
#include <string.h>

#define PROBING_SETTLE_TIME_MS 2000 /* in milli-seconds */

typedef struct udev_device udev_device;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (udev_device, udev_device_unref);


static void     bolt_manager_initable_iface_init (GInitableIface *iface);


static gboolean bolt_manager_initialize (GInitable    *initable,
                                         GCancellable *cancellable,
                                         GError      **error);

static gboolean bolt_manager_store_init (BoltManager *mgr,
                                         GError     **error);

static void     bolt_manager_store_upgrade (BoltManager *mgr,
                                            gboolean    *upgraded);

/* internal manager functions */
static void          manager_sd_notify_status (BoltManager *mgr);

/* domain related functions */
static gboolean      manager_load_domains (BoltManager *mgr,
                                           GError     **error);

static void          manager_bootacl_inital_sync (BoltManager *mgr,
                                                  BoltDomain  *domain);

static BoltDomain *  manager_domain_ensure (BoltManager        *mgr,
                                            struct udev_device *dev);

static BoltDomain *  manager_find_domain_by_syspath (BoltManager *mgr,
                                                     const char  *syspath);

static void          manager_register_domain (BoltManager *mgr,
                                              BoltDomain  *domain);

static void          manager_deregister_domain (BoltManager *mgr,
                                                BoltDomain  *domain);

static void          manager_cleanup_stale_domains (BoltManager *mgr);

/* device related functions */
static gboolean      manager_load_devices (BoltManager *mgr,
                                           GError     **error);

static void          manager_register_device (BoltManager *mgr,
                                              BoltDevice  *device);

static void          manager_deregister_device (BoltManager *mgr,
                                                BoltDevice  *device);

static BoltDevice *  manager_find_device_by_syspath (BoltManager *mgr,
                                                     const char  *sysfs);

static BoltDevice *  manager_find_device_by_uid (BoltManager *mgr,
                                                 const char  *uid,
                                                 GError     **error);

static BoltDevice *  bolt_manager_get_parent (BoltManager *mgr,
                                              BoltDevice  *dev);

static GPtrArray *   bolt_manager_get_children (BoltManager *mgr,
                                                BoltDevice  *target);

static void          bolt_manager_label_device (BoltManager *mgr,
                                                BoltDevice  *target);

/* udev events */
static void         handle_uevent_udev (BoltUdev           *udev,
                                        const char         *action,
                                        struct udev_device *device,
                                        gpointer            user_data);

static void          handle_udev_domain_event (BoltManager        *mgr,
                                               struct udev_device *device,
                                               const char         *action);

static void          handle_udev_domain_removed (BoltManager *mgr,
                                                 BoltDomain  *domain);

static void          handle_udev_device_event (BoltManager        *mgr,
                                               struct udev_device *device,
                                               const char         *action);

static void          handle_udev_device_added (BoltManager        *mgr,
                                               BoltDomain         *domain,
                                               struct udev_device *udev);

static void          handle_udev_device_changed (BoltManager        *mgr,
                                                 BoltDevice         *dev,
                                                 struct udev_device *udev);

static void          handle_udev_device_removed (BoltManager *mgr,
                                                 BoltDevice  *dev);

static void          handle_udev_device_attached (BoltManager        *mgr,
                                                  BoltDomain         *domain,
                                                  BoltDevice         *dev,
                                                  struct udev_device *udev);

static void          handle_udev_device_detached (BoltManager *mgr,
                                                  BoltDevice  *dev);

/* signal callbacks */
static void          handle_store_device_added (BoltStore   *store,
                                                const char  *uid,
                                                BoltManager *mgr);

static void          handle_store_device_removed (BoltStore   *store,
                                                  const char  *uid,
                                                  BoltManager *mgr);

static void          handle_domain_security_changed (BoltManager *mgr,
                                                     GParamSpec  *unused,
                                                     BoltDomain  *domain);

static void          handle_device_status_changed (BoltDevice  *dev,
                                                   BoltStatus   old,
                                                   BoltManager *mgr);

static void          handle_device_generation_changed (BoltDevice  *dev,
                                                       GParamSpec  *unused,
                                                       BoltManager *mgr);

static void          handle_power_state_changed (GObject    *gobject,
                                                 GParamSpec *pspec,
                                                 gpointer    user_data);

/* acquiring indicator  */
static void          manager_probing_device_added (BoltManager        *mgr,
                                                   struct udev_device *dev);

static void          manager_probing_device_removed (BoltManager        *mgr,
                                                     struct udev_device *dev);

static void          manager_probing_domain_added (BoltManager        *mgr,
                                                   struct udev_device *domain);

static void          manager_probing_activity (BoltManager *mgr,
                                               gboolean     weak);

/* force powering */
static BoltGuard *   manager_maybe_power_controller (BoltManager *mgr);

/* config */
static void          manager_load_user_config (BoltManager *mgr);

/* dbus property setter */
static gboolean handle_set_authmode (BoltExported *obj,
                                     const char   *name,
                                     const GValue *value,
                                     GError      **error);

/* dbus method calls */
static GVariant *  handle_list_domains (BoltExported          *object,
                                        GVariant              *params,
                                        GDBusMethodInvocation *invocation,
                                        GError               **error);

static GVariant *  handle_domain_by_id (BoltExported          *object,
                                        GVariant              *params,
                                        GDBusMethodInvocation *invocation,
                                        GError               **error);

static GVariant *  handle_list_devices (BoltExported          *object,
                                        GVariant              *params,
                                        GDBusMethodInvocation *invocation,
                                        GError               **error);

static GVariant *  handle_device_by_uid (BoltExported          *object,
                                         GVariant              *params,
                                         GDBusMethodInvocation *invocation,
                                         GError               **error);

static GVariant *  handle_enroll_device (BoltExported          *object,
                                         GVariant              *params,
                                         GDBusMethodInvocation *invocation,
                                         GError               **error);

static GVariant *  handle_forget_device (BoltExported          *object,
                                         GVariant              *params,
                                         GDBusMethodInvocation *invocation,
                                         GError               **error);

/*  */
struct _BoltManager
{
  BoltExported object;

  /* udev */
  BoltUdev *udev;

  /* state */
  BoltStore   *store;
  BoltDomain  *domains;
  GPtrArray   *devices;
  BoltPower   *power;
  BoltSecurity security;
  BoltAuthMode authmode;
  guint        generation;

  /* policy enforcer */
  BoltBouncer *bouncer;

  /* config */
  GKeyFile  *config;
  BoltPolicy policy;          /* default enrollment policy, unless specified */

  /* probing indicator  */
  guint      authorizing;     /* number of devices currently authorizing */
  GPtrArray *probing_roots;   /* pci device tree root */
  guint      probing_timeout; /* signal id & indicator */
  gint64     probing_tstamp;  /* time stamp of last activity */
  guint      probing_tsettle; /* how long to indicate after the last activity */

  /* watchdog */
  BoltWatchdog *dog;
};

enum {
  PROP_0,

  PROP_VERSION,
  PROP_PROBING,
  PROP_POLICY,
  PROP_SECURITY,
  PROP_AUTHMODE,
  PROP_POWERSTATE,
  PROP_GENERATION,

  PROP_LAST,
  PROP_EXPORTED = PROP_VERSION
};

static GParamSpec *props[PROP_LAST] = {NULL, };

G_DEFINE_TYPE_WITH_CODE (BoltManager,
                         bolt_manager,
                         BOLT_TYPE_EXPORTED,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                bolt_manager_initable_iface_init));


static void
bolt_manager_finalize (GObject *object)
{
  BoltManager *mgr = BOLT_MANAGER (object);

  g_clear_object (&mgr->udev);

  if (mgr->probing_timeout)
    {
      g_source_remove (mgr->probing_timeout);
      mgr->probing_timeout = 0;
    }

  g_clear_pointer (&mgr->probing_roots, g_ptr_array_unref);

  g_clear_object (&mgr->store);
  g_ptr_array_free (mgr->devices, TRUE);
  bolt_domain_clear (&mgr->domains);

  g_clear_object (&mgr->power);
  g_clear_object (&mgr->bouncer);

  g_clear_object (&mgr->dog);

  G_OBJECT_CLASS (bolt_manager_parent_class)->finalize (object);
}


static void
bolt_manager_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  BoltManager *mgr = BOLT_MANAGER (object);

  switch (prop_id)
    {
    case PROP_VERSION:
      g_value_set_uint (value, BOLT_DBUS_API_VERSION);
      break;

    case PROP_PROBING:
      g_value_set_boolean (value, mgr->probing_timeout > 0);
      break;

    case PROP_POLICY:
      g_value_set_enum (value, mgr->policy);
      break;

    case PROP_SECURITY:
      g_value_set_enum (value, mgr->security);
      break;

    case PROP_AUTHMODE:
      g_value_set_flags (value, mgr->authmode);
      break;

    case PROP_POWERSTATE:
      g_value_set_enum (value, bolt_power_get_state (mgr->power));
      break;

    case PROP_GENERATION:
      g_value_set_uint (value, mgr->generation);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bolt_manager_init (BoltManager *mgr)
{
  mgr->devices = g_ptr_array_new_with_free_func (g_object_unref);

  mgr->probing_roots = g_ptr_array_new_with_free_func (g_free);
  mgr->probing_tsettle = PROBING_SETTLE_TIME_MS; /* milliseconds */

  mgr->security = BOLT_SECURITY_UNKNOWN;

  /* default configuration */
  mgr->policy = BOLT_POLICY_AUTO;
  mgr->authmode = BOLT_AUTH_ENABLED;
}

static void
bolt_manager_class_init (BoltManagerClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  BoltExportedClass *exported_class = BOLT_EXPORTED_CLASS (klass);

  gobject_class->finalize = bolt_manager_finalize;
  gobject_class->get_property = bolt_manager_get_property;

  props[PROP_VERSION] =
    g_param_spec_uint ("version", "Version", "Version",
                       0, G_MAXUINT32, 0,
                       G_PARAM_READABLE |
                       G_PARAM_STATIC_STRINGS);

  props[PROP_PROBING] =
    g_param_spec_boolean ("probing", "Probing", "Probing",
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS);

  props[PROP_POLICY] =
    g_param_spec_enum ("default-policy", "DefaultPolicy", "DefaultPolicy",
                       BOLT_TYPE_POLICY,
                       BOLT_POLICY_AUTO,
                       G_PARAM_READABLE |
                       G_PARAM_STATIC_STRINGS);

  props[PROP_SECURITY] =
    g_param_spec_enum ("security-level", "SecurityLevel", NULL,
                       BOLT_TYPE_SECURITY,
                       BOLT_SECURITY_UNKNOWN,
                       G_PARAM_READABLE |
                       G_PARAM_STATIC_STRINGS);

  props[PROP_AUTHMODE] =
    g_param_spec_flags ("auth-mode", "AuthMode", NULL,
                        BOLT_TYPE_AUTH_MODE,
                        BOLT_AUTH_ENABLED,
                        G_PARAM_READABLE |
                        G_PARAM_STATIC_STRINGS);

  props[PROP_POWERSTATE] =
    g_param_spec_enum ("power-state", "PowerState", NULL,
                       BOLT_TYPE_POWER_STATE,
                       BOLT_FORCE_POWER_UNSET,
                       G_PARAM_READABLE |
                       G_PARAM_STATIC_STRINGS);

  props[PROP_GENERATION] =
    g_param_spec_uint ("generation", "Generation", NULL,
                       0, G_MAXUINT, 0,
                       G_PARAM_READABLE |
                       G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, PROP_LAST, props);


  bolt_exported_class_set_interface_info (exported_class,
                                          BOLT_DBUS_INTERFACE,
                                          BOLT_DBUS_GRESOURCE_PATH);

  bolt_exported_class_export_properties (exported_class,
                                         PROP_EXPORTED,
                                         PROP_LAST,
                                         props);

  bolt_exported_class_property_setter (exported_class,
                                       props[PROP_AUTHMODE],
                                       handle_set_authmode);

  bolt_exported_class_export_method (exported_class,
                                     "ListDomains",
                                     handle_list_domains);

  bolt_exported_class_export_method (exported_class,
                                     "DomainById",
                                     handle_domain_by_id);

  bolt_exported_class_export_method (exported_class,
                                     "ListDevices",
                                     handle_list_devices);

  bolt_exported_class_export_method (exported_class,
                                     "DeviceByUid",
                                     handle_device_by_uid);

  bolt_exported_class_export_method (exported_class,
                                     "EnrollDevice",
                                     handle_enroll_device);

  bolt_exported_class_export_method (exported_class,
                                     "ForgetDevice",
                                     handle_forget_device);
}

static void
bolt_manager_initable_iface_init (GInitableIface *iface)
{
  iface->init = bolt_manager_initialize;
}

static gboolean
bolt_manager_initialize (GInitable    *initable,
                         GCancellable *cancellable,
                         GError      **error)
{
  g_autoptr(BoltGuard) power = NULL;
  BoltManager *mgr;
  struct udev_enumerate *enumerate;
  struct udev_list_entry *l, *devices;
  gboolean upgraded = FALSE;
  gboolean ok;

  mgr = BOLT_MANAGER (initable);

  /* store setup */
  ok = bolt_manager_store_init (mgr, error);
  if (!ok)
    return FALSE;

  /* load dynamic user configuration */
  manager_load_user_config (mgr);

  /* polkit setup */
  mgr->bouncer = bolt_bouncer_new (cancellable, error);
  if (mgr->bouncer == NULL)
    return FALSE;

  bolt_bouncer_add_client (mgr->bouncer, mgr);

  /* watchdog setup */
  mgr->dog = bolt_watchdog_new (error);
  if (mgr->dog == NULL)
    return FALSE;

  /* udev setup*/
  bolt_info (LOG_TOPIC ("udev"), "initializing udev");
  mgr->udev = bolt_udev_new ("udev", NULL, error);

  if (mgr->udev == NULL)
    return FALSE;

  g_signal_connect_object (mgr->udev, "uevent",
                           (GCallback) handle_uevent_udev,
                           mgr, 0);

  ok = manager_load_domains (mgr, error);
  if (!ok)
    return FALSE;

  ok = manager_load_devices (mgr, error);
  if (!ok)
    return FALSE;

  /* setup the power controller */
  mgr->power = bolt_power_new (mgr->udev);
  bolt_bouncer_add_client (mgr->bouncer, mgr->power);

  g_signal_connect_object (mgr->power, "notify::state",
                           G_CALLBACK (handle_power_state_changed),
                           mgr, 0);

  /* if we don't see any tb device, we try to force power */
  power = manager_maybe_power_controller (mgr);

  if (power != NULL)
    bolt_info (LOG_TOPIC ("manager"), "acquired power guard '%s'",
               bolt_guard_get_id (power));

  /* TODO: error checking */
  enumerate =  bolt_udev_new_enumerate (mgr->udev, NULL);
  udev_enumerate_add_match_subsystem (enumerate, "thunderbolt");
  /* only devices (i.e. not the domain controller) */

  bolt_info (LOG_TOPIC ("udev"), "enumerating devices");
  udev_enumerate_scan_devices (enumerate);
  devices = udev_enumerate_get_list_entry (enumerate);

  udev_list_entry_foreach (l, devices)
    {
      g_autoptr(GError) err = NULL;
      g_autoptr(udev_device) udevice = NULL;
      const char *syspath;
      const char *devtype;

      syspath = udev_list_entry_get_name (l);
      udevice = bolt_udev_device_new_from_syspath (mgr->udev,
                                                   syspath,
                                                   &err);

      if (udevice == NULL)
        {
          bolt_warn_err (err, "enumerating devices");
          continue;
        }

      devtype = udev_device_get_devtype (udevice);

      if (bolt_streq (devtype, "thunderbolt_domain"))
        handle_udev_domain_event (mgr, udevice, "add");

      if (bolt_streq (devtype, "thunderbolt_device"))
        handle_udev_device_event (mgr, udevice, "add");
    }

  udev_enumerate_unref (enumerate);

  /* upgrade the store, if needed */
  bolt_manager_store_upgrade (mgr, &upgraded);

  if (upgraded)
    manager_cleanup_stale_domains (mgr);

  manager_sd_notify_status (mgr);

  return TRUE;
}

static gboolean
bolt_manager_store_init (BoltManager *mgr, GError **error)
{
  bolt_info (LOG_TOPIC ("manager"), "initializing store");

  mgr->store = bolt_store_new (bolt_get_store_path (), error);
  if (mgr->store == NULL)
    return FALSE;

  g_signal_connect_object (mgr->store, "device-added",
                           G_CALLBACK (handle_store_device_added),
                           mgr, 0);

  g_signal_connect_object (mgr->store, "device-removed",
                           G_CALLBACK (handle_store_device_removed),
                           mgr, 0);

  return TRUE;
}

static void
bolt_manager_store_upgrade (BoltManager *mgr, gboolean *upgraded)
{
  g_autoptr(GError) err = NULL;
  BoltStore *store = mgr->store;
  guint ver;
  gboolean ok;

  ver = bolt_store_get_version (store);

  if (ver == BOLT_STORE_VERSION)
    {
      bolt_debug (LOG_TOPIC ("store"), "store is up to date");
      return;
    }

  bolt_info (LOG_TOPIC ("store"), "attempting upgrade from '%d'",
             ver);

  ok = bolt_store_upgrade (store, NULL, &err);
  if (!ok)
    {
      bolt_warn_err (err, LOG_TOPIC ("store"), "upgrade failed");
      return;
    }

  ver = bolt_store_get_version (store);
  bolt_info (LOG_TOPIC ("store"), "upgraded to version '%d'",
             ver);

  *upgraded = TRUE;
}

/* internal functions */
static void
manager_sd_notify_status (BoltManager *mgr)
{
  g_autoptr(GError) err = NULL;
  g_autofree char *status = NULL;
  const char *pstate = NULL;
  BoltPowerState power;
  gboolean sent;
  gboolean authorizing;
  gboolean ok = FALSE;

  authorizing = bolt_flag_isset (mgr->authmode, BOLT_AUTH_ENABLED);
  power = bolt_power_get_state (mgr->power);
  pstate = bolt_enum_to_string (BOLT_TYPE_POWER_STATE, power, NULL);

  status = g_strdup_printf ("STATUS=authmode: %s,"
                            " force-power: %s",
                            (authorizing ? "enabled" : "DISABLED"),
                            (pstate ? : "unknown"));

  ok = bolt_sd_notify_literal (status, &sent, &err);

  if (!ok)
    bolt_warn_err (err, LOG_TOPIC ("status"),
                   "failed to send status");
  else
    bolt_debug (LOG_TOPIC ("status"), "%s [sent: %s]",
                status, bolt_yesno (sent));
}

static void
manager_maybe_set_security (BoltManager *mgr,
                            BoltSecurity security)
{
  /* update the security level, if it is not already
  * set, but also ignore if security is 'unknown' */

  if (security == BOLT_SECURITY_UNKNOWN)
    return;

  if (mgr->security == BOLT_SECURITY_UNKNOWN)
    {
      bolt_info ("security level set to '%s'",
                 bolt_security_to_string (security));
      mgr->security = security;
      g_object_notify_by_pspec (G_OBJECT (mgr),
                                props[PROP_SECURITY]);
    }
  else if (mgr->security != security)
    {
      bolt_warn ("multiple security levels (%s vs %s)",
                 bolt_security_to_string (mgr->security),
                 bolt_security_to_string (security));
    }
}

static void
manager_maybe_set_generation (BoltManager *mgr,
                              guint        gen)
{
  guint check;

  check = MAX (mgr->generation, gen);

  if (mgr->generation >= check)
    return;

  bolt_info ("global 'generation' set to '%d'", check);

  mgr->generation = check;
  g_object_notify_by_pspec (G_OBJECT (mgr),
                            props[PROP_GENERATION]);
}

/* domain related function */
static gboolean
manager_load_domains (BoltManager *mgr,
                      GError     **error)
{
  g_auto(GStrv) ids = NULL;

  ids = bolt_store_list_uids (mgr->store, "domains", error);
  if (ids == NULL)
    {
      g_prefix_error (error, "failed to list domains in store: ");
      return FALSE;
    }

  bolt_info (LOG_TOPIC ("store"), "loading domains");

  for (guint i = 0; i < g_strv_length (ids); i++)
    {
      g_autoptr(GError) err = NULL;
      BoltDomain *dom = NULL;
      const char *uid = ids[i];

      bolt_info (LOG_TOPIC ("store"), LOG_DOM_UID (uid),
                 "loading domain");

      dom = bolt_store_get_domain (mgr->store, uid, &err);
      if (dom == NULL)
        {
          bolt_warn_err (err, LOG_DOM_UID (uid), LOG_TOPIC ("store"),
                         "failed to load domain", uid);
          continue;
        }

      manager_register_domain (mgr, dom);
    }

  return TRUE;
}

static void
manager_bootacl_inital_sync (BoltManager *mgr,
                             BoltDomain  *domain)
{
  g_autoptr(GError) err = NULL;
  g_auto(GStrv) acl = NULL;
  gboolean ok;
  guint n, empty;

  if (!bolt_domain_supports_bootacl (domain))
    {
      bolt_info (LOG_TOPIC ("bootacl"), LOG_DOM (domain),
                 "bootacl not supported, no sync");
      return;
    }

  acl = bolt_domain_dup_bootacl (domain);
  g_assert (acl != NULL);

  n = bolt_domain_bootacl_slots (domain, &empty);

  bolt_info (LOG_TOPIC ("bootacl"), LOG_DOM (domain),
             "sync start [slots: %u free: %u]", n, empty);

  for (guint i = 0; i < mgr->devices->len; i++)
    {
      BoltDevice *dev = g_ptr_array_index (mgr->devices, i);
      const char *duid = bolt_device_get_uid (dev);
      gboolean polok, inacl, sync;

      polok = bolt_device_get_policy (dev) == BOLT_POLICY_AUTO;
      inacl = bolt_domain_bootacl_contains (domain, duid);
      sync = polok && !inacl;

      bolt_info (LOG_TOPIC ("bootacl"), LOG_DOM (domain),
                 LOG_DEV_UID (duid),
                 "sync '%.13s…' %s [policy: %3s, in acl: %3s]",
                 duid, bolt_yesno (sync), bolt_yesno (polok),
                 bolt_yesno (inacl));

      if (!sync)
        continue;

      bolt_domain_bootacl_allocate (domain, acl, duid);
    }

  ok = bolt_domain_bootacl_set (domain, acl, &err);

  if (!ok && err != NULL)
    {
      bolt_warn_err (err, LOG_DOM (domain), "failed to write bootacl");
      return;
    }

  bolt_domain_bootacl_slots (domain, &empty);
  bolt_info (LOG_TOPIC ("bootacl"), LOG_DOM (domain),
             "sync done [wrote: %s, now free: %u]",
             bolt_yesno (ok), empty);
}

static gboolean
domain_has_stable_uuid (BoltDomain         *domain,
                        struct udev_device *dev)
{
  g_autoptr(GError) err = NULL;
  gboolean stable;
  gboolean ok;
  guint32 pci_id;

  /* On integrated TBT, like ICL/TGL, the uuid of the
   * controller is randomly generated on *every* boot,
   * and thus the uuid is not stable. */

  /* default to FALSE, in case we have no entry */
  stable = FALSE;

  ok = bolt_sysfs_nhi_id_for_domain (dev, &pci_id, &err);
  if (!ok)
    {
      bolt_warn_err (err, LOG_TOPIC ("udev"), LOG_DOM (domain),
                     "failed to get NHI for domain");
      return FALSE;
    }

  ok = bolt_nhi_uuid_is_stable (pci_id, &stable, &err);
  if (!ok)
    bolt_warn_err (err, LOG_TOPIC ("udev"), LOG_DOM (domain),
                   "failed to determine if uid is stable");

  bolt_info (LOG_TOPIC ("udev"), LOG_DOM (domain),
             "uuid is stable: %s (for NHI: 0x%04x)",
             bolt_yesno (stable), pci_id);

  return stable;
}

static void
manager_store_domain (BoltManager *mgr,
                      BoltDomain  *domain)
{
  g_autoptr(GError) err = NULL;
  gboolean ok;

  bolt_info (LOG_TOPIC ("store"), LOG_DOM (domain),
             "storing newly connected domain");

  ok = bolt_store_put_domain (mgr->store, domain, &err);
  if (!ok)
    bolt_warn_err (err, LOG_TOPIC ("store"), LOG_DOM (domain),
                   "could not store domain");
}

static BoltDomain *
manager_domain_ensure (BoltManager        *mgr,
                       struct udev_device *dev)
{
  g_autoptr(GError) err = NULL;
  BoltSecurity level;
  BoltDomain *domain = NULL;
  GDBusConnection *bus;
  struct udev_device *host;
  struct udev_device *dom;
  const char *security;
  const char *syspath;
  const char *op;
  const char *uid;
  gboolean iommu;

  /* check if we already know a domain that is the parent
   * of the device (dev); if not then 'dev' is very likely
   * the host device.
   */
  syspath = udev_device_get_syspath (dev);
  domain = manager_find_domain_by_syspath (mgr, syspath);

  if (domain != NULL)
    return domain;

  /* dev is very likely the host, i.e. root switch device,
   * but we might as well make sure we can get the domain
   * from any; also we make sure that we have indeed the
   * host device via this lookup.
   */
  dom = bolt_sysfs_domain_for_device (dev, &host);
  if (dom == NULL)
    return NULL;

  uid = bolt_sysfs_device_get_unique_id (host, NULL);

  /* check if we have a stored domain with a matching uid
   * of the host device, if so then we have just connected
   * the corresponding domain controller, represented by
   * the 'dom' udev_device.
   */
  domain = bolt_domain_find_id (mgr->domains, uid, NULL);

  if (domain != NULL)
    {
      bolt_domain_connected (domain, dom);
      return domain;
    }

  /* this is an unknown, unstored domain controller */
  domain = bolt_domain_new_for_udev (dom, uid, &err);

  if (domain == NULL)
    {
      bolt_warn_err (err, LOG_TOPIC ("udev"),
                     "failed to create domain: %s");
      return NULL;
    }

  level = bolt_domain_get_security (domain);
  iommu = bolt_domain_has_iommu (domain);
  security = bolt_security_for_display (level, iommu);

  bolt_msg (LOG_DOM (domain), "newly connected [%s] (%s)",
            security, syspath);

  manager_maybe_set_security (mgr, level);

  manager_register_domain (mgr, domain);

  /* registering the domain will add one reference */
  g_object_unref (domain);

  /* add all devices with POLICY_AUTO to the bootacl */
  manager_bootacl_inital_sync (mgr, domain);

  /* now store the domain (with an updated bootacl),
   * but only if its uuid is the same across reboots */
  if (domain_has_stable_uuid (domain, dom))
    manager_store_domain (mgr, domain);

  /* export it on the bus and emit the added signals */
  bus = bolt_exported_get_connection (BOLT_EXPORTED (mgr));
  if (bus == NULL)
    return domain;

  bolt_domain_export (domain, bus);

  op = bolt_exported_get_object_path (BOLT_EXPORTED (domain));
  bolt_exported_emit_signal (BOLT_EXPORTED (mgr),
                             "DomainAdded",
                             g_variant_new ("(o)", op),
                             NULL);

  return domain;
}

static BoltDomain *
manager_find_domain_by_syspath (BoltManager *mgr,
                                const char  *syspath)
{
  BoltDomain *iter = mgr->domains;
  guint n_domains;

  n_domains = bolt_domain_count (mgr->domains);
  for (guint i = 0; i < n_domains; i++)
    {
      const char *prefix = bolt_domain_get_syspath (iter);

      /* we get a perfect match, if we search for the domain
       * itself, or if we are looking for the domain that
       * is the parent of the device in @syspath */
      if (prefix && g_str_has_prefix (syspath, prefix))
        return iter;

      iter = bolt_domain_next (iter);
    }

  return NULL;
}

static gboolean
bootacl_alloc (BoltDomain  *domain,
               GStrv        acl,
               const char  *uid,
               gint        *slot,
               BoltManager *mgr)
{
  char **target = NULL;
  guint64 last = G_MAXUINT64;

  /* if we have a valid allocation, just use that */
  if (*slot != -1)
    return TRUE;

  g_return_val_if_fail (acl != NULL, FALSE);

  /* we need to replace one of devices currently in the acl */
  for (char **iter = acl; *iter; iter++)
    {
      g_autoptr(BoltDevice) dev = NULL;
      guint64 ts;

      dev = manager_find_device_by_uid (mgr, uid, NULL);

      /* device might not be managed by us */
      if (dev == NULL)
        continue;

      /* authtime is the last time a device was seen and
       * successfully authorized; choose the one that
       * has the *oldest* timestamp (smallest number) */
      ts = bolt_device_get_authtime (dev);
      if (ts < last)
        {
          target = iter;
          last = ts;
        }
    }

  if (target == NULL)
    return FALSE;

  *slot = target - acl;

  return TRUE;
}


static void
manager_register_domain (BoltManager *mgr,
                         BoltDomain  *domain)
{
  guint n_slots, n_free;

  mgr->domains = bolt_domain_insert (mgr->domains, domain);

  n_slots = bolt_domain_bootacl_slots (domain, &n_free);

  bolt_info (LOG_TOPIC ("domain"), LOG_DOM (domain),
             "registered (bootacl: %u/%u)",
             n_free, n_slots);

  g_signal_connect_object (domain, "bootacl-alloc",
                           G_CALLBACK (bootacl_alloc), mgr, 0);

  g_signal_connect_object (domain, "notify::security",
                           G_CALLBACK (handle_domain_security_changed),
                           mgr, G_CONNECT_SWAPPED);

  bolt_bouncer_add_client (mgr->bouncer, domain);
}

static void
manager_deregister_domain (BoltManager *mgr,
                           BoltDomain  *domain)
{
  bolt_info (LOG_TOPIC ("manager"), LOG_DOM (domain),
             "de-registered");

  mgr->domains = bolt_domain_remove (mgr->domains, domain);
}

static void
manager_cleanup_stale_domains (BoltManager *mgr)
{
  g_autoptr(GPtrArray) domains = NULL;
  BoltDomain *iter;
  guint count;

  /* Before bolt 0.9.1, domains were stored that have an
   * unstable uuid, i.e. their uuids change on every boot
   * and thus there will be store entries that can't be
   * matched anymore and thus are "stale".
   */

  bolt_info (LOG_TOPIC ("manager"), "stale domain cleanup");

  count = bolt_domain_count (mgr->domains);
  domains = g_ptr_array_new_full (count, g_object_unref);

  iter = mgr->domains;
  for (guint i = 0; i < count; i++)
    {
      gboolean stored = bolt_domain_is_stored (iter);
      gboolean offline = !bolt_domain_is_connected (iter);

      if (stored && offline)
        g_ptr_array_add (domains, g_object_ref (iter));

      iter = bolt_domain_next (iter);
    }

  for (guint i = 0; i < domains->len; i++)
    {
      g_autoptr(GError) err = NULL;
      BoltDomain *dom = g_ptr_array_index (domains, i);
      gboolean ok;

      bolt_info (LOG_DOM (dom), LOG_TOPIC ("store"),
                 "stale domain detected");

      ok = bolt_store_del_domain (mgr->store, dom, &err);

      if (!ok)
        {
          bolt_warn_err (err, LOG_DOM (dom),
                         "failed to delete domain");
          continue;
        }

      manager_deregister_domain (mgr, dom);
    }
}

/* device related functions */
static gboolean
manager_load_devices (BoltManager *mgr,
                      GError     **error)
{
  g_auto(GStrv) ids = NULL;

  ids = bolt_store_list_uids (mgr->store, "devices", error);
  if (ids == NULL)
    {
      g_prefix_error (error, "failed to list devices in store: ");
      return FALSE;
    }

  bolt_info (LOG_TOPIC ("store"), "loading devices");
  for (guint i = 0; i < g_strv_length (ids); i++)
    {
      g_autoptr(GError) err = NULL;
      BoltDevice *dev = NULL;
      const char *uid = ids[i];

      bolt_info (LOG_DEV_UID (uid), LOG_TOPIC ("store"), "loading device");

      dev = bolt_store_get_device (mgr->store, uid, &err);
      if (dev == NULL)
        {
          bolt_warn_err (err, LOG_TOPIC ("store"),
                         LOG_DIRECT (BOLT_LOG_DEVICE_UID, uid),
                         "failed to load device (%.7s)", uid);
          continue;
        }

      manager_register_device (mgr, dev);
    }

  return TRUE;
}

static void
manager_register_device (BoltManager *mgr,
                         BoltDevice  *dev)
{

  g_ptr_array_add (mgr->devices, dev);
  bolt_bouncer_add_client (mgr->bouncer, dev);
  g_signal_connect_object (dev, "status-changed",
                           G_CALLBACK (handle_device_status_changed),
                           mgr, 0);

  if (bolt_device_is_host (dev))
    {
      guint generation = bolt_device_get_generation (dev);
      manager_maybe_set_generation (mgr, generation);
      g_signal_connect_object (dev, "notify::generation",
                               G_CALLBACK (handle_device_generation_changed),
                               mgr, 0);
    }

}

static void
manager_deregister_device (BoltManager *mgr,
                           BoltDevice  *dev)
{
  const char *opath;

  g_ptr_array_remove_fast (mgr->devices, dev);

  opath = bolt_device_get_object_path (dev);

  if (opath == NULL)
    return;

  bolt_exported_emit_signal (BOLT_EXPORTED (mgr),
                             "DeviceRemoved",
                             g_variant_new ("(o)", opath),
                             NULL);

  bolt_device_unexport (dev);
  bolt_info (LOG_DEV (dev), LOG_TOPIC ("dbus"), "unexported");
}

static BoltDevice *
manager_find_device_by_syspath (BoltManager *mgr,
                                const char  *sysfs)
{

  g_return_val_if_fail (sysfs != NULL, NULL);

  for (guint i = 0; i < mgr->devices->len; i++)
    {
      BoltDevice *dev = g_ptr_array_index (mgr->devices, i);
      const char *have = bolt_device_get_syspath (dev);

      if (bolt_streq (have, sysfs))
        return g_object_ref (dev);

    }

  return NULL;
}

static BoltDevice *
manager_find_device_by_uid (BoltManager *mgr,
                            const char  *uid,
                            GError     **error)
{
  if (uid == NULL || uid[0] == '\0')
    {
      g_set_error_literal (error, G_IO_ERROR,
                           G_IO_ERROR_INVALID_ARGUMENT,
                           "empty device unique_id");
      return NULL;
    }

  for (guint i = 0; i < mgr->devices->len; i++)
    {
      BoltDevice *dev = g_ptr_array_index (mgr->devices, i);

      if (bolt_streq (bolt_device_get_uid (dev), uid))
        return g_object_ref (dev);

    }

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
               "device with id '%s' could not be found.",
               uid);

  return NULL;
}


static BoltDevice *
bolt_manager_get_parent (BoltManager *mgr,
                         BoltDevice  *dev)
{
  g_autofree char *path = NULL;
  const char *syspath;
  const char *start;
  char *pos;

  syspath = bolt_device_get_syspath (dev);
  if (syspath == NULL)
    return NULL;

  path = g_strdup (syspath);
  start = path + strlen ("/sys");

  pos = strrchr (start, '/');
  if (!pos || pos < start + 2)
    return NULL;

  *pos = '\0';

  return manager_find_device_by_syspath (mgr, path);
}

static GPtrArray *
bolt_manager_get_children (BoltManager *mgr,
                           BoltDevice  *target)
{
  GPtrArray *res;

  res = g_ptr_array_new_with_free_func (g_object_unref);

  for (guint i = 0; i < mgr->devices->len; i++)
    {
      g_autoptr(BoltDevice) parent = NULL;
      BoltDevice *dev = g_ptr_array_index (mgr->devices, i);

      parent = bolt_manager_get_parent (mgr, dev);
      if (parent != target)
        continue;

      g_ptr_array_add (res, g_object_ref (dev));
    }

  return res;
}

static void
bolt_manager_label_device (BoltManager *mgr,
                           BoltDevice  *target)
{
  g_autofree char *label = NULL;
  const char *name;
  const char *vendor;
  guint count = 0;
  static struct
  {
    const char *from;
    const char *to;
  } vendors[] = {
    {"HP Inc.",     "HP"   },
    {"Apple, Inc.", "Apple"}
  };

  name = bolt_device_get_name (target);
  vendor = bolt_device_get_vendor (target);

  /* we count how many duplicate devices we have */
  for (guint i = 0; i < mgr->devices->len; i++)
    {
      BoltDevice *dev = g_ptr_array_index (mgr->devices, i);
      const char *dev_name = bolt_device_get_name (dev);
      const char *dev_vendor = bolt_device_get_vendor (dev);

      if (bolt_streq (dev_name, name) &&
          bolt_streq (dev_vendor, vendor))
        count++;
    }

  /* cleanup name: nicer display names for vendors  */
  for (guint i = 0; i < G_N_ELEMENTS (vendors); i++)
    if (bolt_streq (vendor, vendors[i].from))
      vendor = vendors[i].to;

  /* cleanup name: Vendor Vendor Device -> Vendor Device */
  if (g_str_has_prefix (name, vendor))
    {
      name += strlen (vendor);

      while (g_ascii_isspace (*name))
        name++;

      if (*name == '\0')
        name = bolt_device_get_name (target);
    }

  /* we counted the target too, > 1 means duplicates */
  if (count > 1)
    label = g_strdup_printf ("%s %s #%u", vendor, name, count);
  else
    label = g_strdup_printf ("%s %s", vendor, name);

  bolt_info (LOG_DEV (target), "labeling device: %s", label);
  g_object_set (G_OBJECT (target), "label", label, NULL);
}

/* device authorization */
static void
auto_auth_done (GObject      *source,
                GAsyncResult *res,
                gpointer      user_data)
{
  g_autoptr(GError) err = NULL;
  BoltDevice *dev = BOLT_DEVICE (source);
  BoltAuth *auth = BOLT_AUTH (res);
  gboolean ok;

  ok = bolt_auth_check (auth, &err);

  if (!ok)
    bolt_warn_err (err, LOG_DEV (dev), LOG_TOPIC ("auto-auth"),
                   "authorization failed");
  else
    bolt_msg (LOG_DEV (dev), LOG_TOPIC ("auto-auth"),
              "authorization successful");
}

static void
manager_auto_authorize (BoltManager *mgr,
                        BoltDevice  *dev)
{
  g_autoptr(BoltAuth) auth = NULL;
  g_autoptr(BoltKey) key = NULL;
  g_autofree char *amstr = NULL;
  BoltStatus status;
  BoltPolicy policy;
  gboolean authmode;
  gboolean authorize;
  gboolean iommu;
  BoltSecurity level;
  gboolean ok;

  status = bolt_device_get_status (dev);
  policy = bolt_device_get_policy (dev);

  /* more of a sanity check, because we should only end up
  * here if the device is not yet authorized and stored */
  if (bolt_status_is_authorized (status) ||
      !bolt_device_get_stored (dev))
    return;

  /* The default is not to authorize anything */
  authorize = FALSE;

  /* The following sections are security critical and thus
   * intentionally verbose and explicit  */

  /* 1) global auth mode and policy check */
  authmode = bolt_auth_mode_is_enabled (mgr->authmode);
  amstr = bolt_auth_mode_to_string (authmode);

  iommu = bolt_device_has_iommu (dev);
  level = bolt_device_get_security (dev);

  if (authmode)
    {
      if (policy == BOLT_POLICY_AUTO)
        authorize = TRUE;
      else if (policy == BOLT_POLICY_IOMMU && iommu)
        authorize = TRUE;
    }

  bolt_msg (LOG_DEV (dev), LOG_TOPIC ("auto-auth"),
            "authmode: %s, policy: %s, iommu: %s -> %s",
            amstr, bolt_policy_to_string (policy),
            bolt_yesno (iommu), bolt_okfail (authorize));

  if (!authorize)
    return;

  /* 2) security level and key check: if we are in SECURE
   *    mode but don't have a key, we DON'T authorize */
  if (level == BOLT_SECURITY_SECURE)
    {
      g_autoptr(GError) err = NULL;

      ok = bolt_device_load_key (dev, &key, &err);
      if (!ok)
        bolt_warn_err (err, LOG_DEV (dev), "could not load key");

      authorize = (key != NULL);
    }

  bolt_msg (LOG_DEV (dev), LOG_TOPIC ("auto-auth"),
            "security: %s mode, key: %s -> %s",
            bolt_security_for_display (level, iommu),
            bolt_yesno (key), bolt_okfail (authorize));

  if (!authorize)
    return;

  auth = bolt_auth_new (mgr, level, key);
  bolt_device_authorize_idle (dev, auth, auto_auth_done, mgr);
}

static void
manager_do_import_device (BoltManager *mgr,
                          BoltDevice  *dev,
                          BoltPolicy   policy)
{
  g_autoptr(GError) err = NULL;
  gboolean ok;

  ok = bolt_store_put_device (mgr->store,
                              dev,
                              policy,
                              NULL,
                              &err);

  if (!ok)
    bolt_warn_err (err, LOG_DEV (dev), LOG_TOPIC ("import"),
                   "failed to store device");
}

static void
manager_maybe_import (BoltManager *mgr,
                      BoltDevice  *dev)
{
  BoltSecurity level;
  BoltPolicy policy;
  const char *secstr;
  const char *polstr;
  gboolean sl0, sl1;
  gboolean boot, pcie;
  gboolean import;
  gboolean iommu;

  /* This function is intentionally more verbose then it
   * could be, but since it is security critical, it is
   * better to be clear than concise */

  g_return_if_fail (!bolt_device_get_stored (dev));
  g_return_if_fail (bolt_device_is_authorized (dev));

  if (bolt_device_is_host (dev))
    {
      BoltDomain *dom = bolt_device_get_domain (dev);

      /* host devices are per design authorized and
       * must therefore never be authorized by bolt */
      policy = BOLT_POLICY_MANUAL;

      /* Store the host device only if its domain is
       * stored as well. Currently, the only reason
       * for a domain to not be stored is that its
       * uuid is not stable, i.e. changes every boot.
       * But its uuid is in fact derived from the
       * associated host device, i.e. this very host
       * device here. Ergo, its uuid is unstable and
       * thus it should not be stored */
      if (bolt_domain_is_stored (dom))
        manager_do_import_device (mgr, dev, policy);

      return;
    }

  level = bolt_device_get_security (dev);
  iommu = bolt_device_has_iommu (dev);
  boot = bolt_device_check_authflag (dev, BOLT_AUTH_BOOT);

  pcie = bolt_security_allows_pcie (level);
  sl0 = level == BOLT_SECURITY_NONE;
  sl1 = level == BOLT_SECURITY_USER;

  /* Check if we want to import that device at all,
   * the fundamental rule is: if it was authorized
   * by the firmware and we have pcie tunnels we want
   * to import it so we have a record of it */
  import = pcie && (boot || sl0);

  if (import && !iommu && sl1)
    policy = BOLT_POLICY_AUTO;
  else
    policy = BOLT_POLICY_IOMMU;

  secstr = bolt_security_for_display (level, iommu);
  polstr = bolt_policy_to_string (policy);

  bolt_msg (LOG_DEV (dev), LOG_TOPIC ("import"),
            "%s mode, boot: %s -> %s",
            secstr, bolt_yesno (boot),
            (import ? polstr : "no import"));

  if (import)
    manager_do_import_device (mgr, dev, policy);
}

static void
auto_enroll_done (GObject      *device,
                  GAsyncResult *res,
                  gpointer      user_data)
{
  g_autoptr(GError) err = NULL;
  BoltDevice *dev = BOLT_DEVICE (device);
  BoltAuth *auth = BOLT_AUTH (res);
  BoltManager *mgr;
  BoltPolicy policy;
  BoltKey *key;
  gboolean ok;

  mgr = BOLT_MANAGER (bolt_auth_get_origin (auth));
  ok = bolt_auth_check (auth, &err);

  if (!ok)
    {
      bolt_warn_err (err, LOG_DEV (dev), LOG_TOPIC ("auto-enroll"),
                     "failed to authorize the new device");
      return;
    }

  key = bolt_auth_get_key (auth);
  policy = bolt_auth_get_policy (auth);

  ok = bolt_store_put_device (mgr->store, dev, policy, key, &err);

  if (ok)
    bolt_msg (LOG_DEV (dev), LOG_TOPIC ("auto-enroll"), "done");
  else
    bolt_warn_err (err, LOG_DEV (dev), LOG_TOPIC ("auto-enroll"),
                   "failed to store the device");
}

static BoltAuth *
manager_enroll_device_prepare (BoltManager *mgr,
                               BoltDevice  *dev,
                               GError     **error)
{
  g_autoptr(BoltDevice) parent = NULL;
  g_autoptr(BoltKey) key = NULL;
  BoltSecurity level;
  BoltAuth *auth;

  /* are we even allowed to enroll new devices right now? */
  if (bolt_auth_mode_is_disabled (mgr->authmode))
    {
      g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED,
                   "authorization of new devices is disabled");
      return NULL;
    }

  /* if the parent is not authorized and we try to authorize,
   * we will fail and log a warning, so lets check, fail early
   * and avoid the warning */
  parent = bolt_manager_get_parent (mgr, dev);
  if (!bolt_device_is_authorized (parent))
    {
      g_set_error (error, BOLT_ERROR, BOLT_ERROR_AUTHCHAIN,
                   "parent not authorized, deferring");
      return NULL;
    }

  /* determine the maximum security level we can use, i.e.
   * minimum level that the device *and* host support */
  if (bolt_device_supports_secure_mode (dev))
    level = bolt_device_get_security (dev);
  else
    level = BOLT_SECURITY_USER;

  /* yay, we are in SECURE mode, we need to create a key */
  if (level == BOLT_SECURITY_SECURE)
    {
      key = bolt_key_new (error);
      if (key == NULL)
        return NULL;
    }

  auth = bolt_auth_new (mgr, level, key);

  /* bolt_auth_new is a wrapper around g_object_new and
   * therefore cannot realistically fail. This assertion
   * convinces clang's static analysis of that fact. */
  g_assert (auth != NULL);

  return auth;
}

static void
manager_auto_enroll (BoltManager *mgr,
                     BoltDevice  *dev)
{
  g_autoptr(BoltAuth) auth = NULL;
  g_autoptr(GError) err = NULL;
  BoltStatus status;
  gboolean have_key;

  g_return_if_fail (!bolt_device_get_stored (dev));

  /* sanity check for the state */
  status = bolt_device_get_status (dev);
  if (!bolt_status_is_pending (status))
    return;

  /* sanity check for iommu */
  if (!bolt_device_has_iommu (dev))
    return;

  auth = manager_enroll_device_prepare (mgr, dev, &err);

  if (!auth)
    {
      bolt_msg (LOG_DEV (dev), LOG_TOPIC ("auto-enroll"),
                "no, pre-check failed: %s", err->message);
      return;
    }

  have_key = bolt_auth_has_key (auth);
  bolt_msg (LOG_DEV (dev), LOG_TOPIC ("auto-enroll"),
            "yes, key: %s", bolt_yesno (have_key));

  bolt_auth_set_policy (auth, BOLT_POLICY_IOMMU);
  bolt_device_authorize (dev, auth, auto_enroll_done, mgr);
}

/* udev callbacks */
static void
handle_uevent_udev (BoltUdev           *udev,
                    const char         *action,
                    struct udev_device *device,
                    gpointer            user_data)
{
  BoltManager *mgr;
  const char *subsystem;
  const char *devtype;
  const char *syspath;

  mgr = BOLT_MANAGER (user_data);

  devtype = udev_device_get_devtype (device);
  subsystem = udev_device_get_subsystem (device);
  syspath = udev_device_get_syspath (device);

  if (g_str_equal (action, "add"))
    manager_probing_device_added (mgr, device);
  else if (g_str_equal (action, "remove"))
    manager_probing_device_removed (mgr, device);

  /* beyond this point only udev device from the
   * thunderbolt are handled */
  if (!bolt_streq (subsystem, "thunderbolt"))
    return;

  bolt_debug (LOG_TOPIC ("udev"), "%s (%s%s%s) %s", action,
              subsystem, devtype ? "/" : "", devtype ? : "",
              syspath);

  if (bolt_streq (devtype, "thunderbolt_device"))
    handle_udev_device_event (mgr, device, action);
  else if (bolt_streq (devtype, "thunderbolt_domain"))
    handle_udev_domain_event (mgr, device, action);
}

static void
handle_udev_domain_event (BoltManager        *mgr,
                          struct udev_device *device,
                          const char         *action)
{
  const char *syspath;
  BoltDomain *domain;

  syspath = udev_device_get_syspath (device);

  if (g_str_equal (action, "add"))
    {
      manager_probing_domain_added (mgr, device);

      /* the creation of the actual domain object and
       * its registration is handled on-demand: only
       * when the host device appears, the uevent
       * handler for the thunderbolt devices will
       * ensure the domain exists
       */
    }
  else if (g_str_equal (action, "change"))
    {
      domain = manager_find_domain_by_syspath (mgr, syspath);

      if (domain == NULL)
        {
          bolt_warn (LOG_TOPIC ("domain"),
                     "unregistered domain changed at %s",
                     syspath);
          return;
        }

      bolt_domain_update_from_udev (domain, device);
    }
  else if (g_str_equal (action, "remove"))
    {
      domain = manager_find_domain_by_syspath (mgr, syspath);

      if (!domain)
        {
          bolt_warn (LOG_TOPIC ("domain"),
                     "unregistered domain removed at %s",
                     syspath);
          return;
        }

      if (bolt_domain_is_stored (domain))
        bolt_domain_disconnected (domain);
      else
        handle_udev_domain_removed (mgr, domain);
    }
}

static void
handle_udev_domain_removed (BoltManager *mgr,
                            BoltDomain  *domain)
{
  const char *name;

  name = bolt_domain_get_id (domain);
  bolt_info (LOG_TOPIC ("domain"), "'%s' removed", name);

  if (bolt_exported_is_exported (BOLT_EXPORTED (domain)))
    {
      const char *op;
      gboolean ok;

      op = bolt_exported_get_object_path (BOLT_EXPORTED (domain));
      bolt_exported_emit_signal (BOLT_EXPORTED (mgr),
                                 "DomainRemoved",
                                 g_variant_new ("(o)", op),
                                 NULL);

      ok = bolt_exported_unexport (BOLT_EXPORTED (domain));

      bolt_info (LOG_TOPIC ("dbus"), "%s unexported: %s",
                 name, bolt_okfail (ok));
    }

  manager_deregister_domain (mgr, domain);
}

static void
handle_udev_device_event (BoltManager        *mgr,
                          struct udev_device *device,
                          const char         *action)
{
  g_autoptr(BoltDevice) dev = NULL;
  const char *syspath;

  syspath = udev_device_get_syspath (device);

  if (g_str_equal (action, "add") ||
      g_str_equal (action, "change"))
    {
      BoltDomain *dom;
      const char *uid;

      /* filter sysfs devices (e.g. the domain) that don't have
       * the unique_id attribute */
      uid = bolt_sysfs_device_get_unique_id (device, NULL);
      if (uid == NULL)
        return;

      dom = manager_domain_ensure (mgr, device);

      if (dom == NULL)
        {
          bolt_warn (LOG_TOPIC ("domain"),
                     "could not find domain for device at '%s'",
                     syspath);
          return;
        }

      dev = manager_find_device_by_uid (mgr, uid, NULL);

      if (!dev)
        handle_udev_device_added (mgr, dom, device);
      else if (!bolt_device_is_connected (dev))
        handle_udev_device_attached (mgr, dom, dev, device);
      else
        handle_udev_device_changed (mgr, dev, device);
    }
  else if (g_str_equal (action, "remove"))
    {
      const char *name;

      /* filter out the domain controller */
      name = udev_device_get_sysname (device);
      if (name && g_str_has_prefix (name, "domain"))
        return;

      dev = manager_find_device_by_syspath (mgr, syspath);

      /* if we don't have any records of the device,
       *  then we don't care */
      if (!dev)
        return;

      if (bolt_device_get_stored (dev))
        handle_udev_device_detached (mgr, dev);
      else
        handle_udev_device_removed (mgr, dev);
    }
}

static void
handle_udev_device_added (BoltManager        *mgr,
                          BoltDomain         *domain,
                          struct udev_device *udev)
{
  g_autoptr(GError) err = NULL;
  GDBusConnection *bus;
  BoltDevice *dev;
  BoltStatus status;
  const char *opath;
  const char *syspath;

  syspath = udev_device_get_syspath (udev);

  dev = bolt_device_new_for_udev (udev, domain, &err);
  if (dev == NULL)
    {
      bolt_warn_err (err, LOG_TOPIC ("udev"),
                     "could not create device for %s",
                     syspath);
      return;
    }

  manager_register_device (mgr, dev);

  status = bolt_device_get_status (dev);
  bolt_msg (LOG_DEV (dev), "device added, status: %s, at %s",
            bolt_status_to_string (status), syspath);

  bolt_manager_label_device (mgr, dev);

  if (bolt_status_is_authorized (status))
    manager_maybe_import (mgr, dev);
  else if (bolt_domain_has_iommu (domain))
    manager_auto_enroll (mgr, dev);

  /* if we have a valid dbus connection */
  bus = bolt_exported_get_connection (BOLT_EXPORTED (mgr));
  if (bus == NULL)
    return;

  opath = bolt_device_export (dev, bus, &err);
  if (opath == NULL)
    bolt_warn_err (err, LOG_DEV (dev), LOG_TOPIC ("dbus"), "error exporting");
  else
    bolt_info (LOG_DEV (dev), LOG_TOPIC ("dbus"),
               "exported device at %.43s...", opath);

  bolt_exported_emit_signal (BOLT_EXPORTED (mgr),
                             "DeviceAdded",
                             g_variant_new ("(o)", opath),
                             NULL);
}

static void
handle_udev_device_changed (BoltManager        *mgr,
                            BoltDevice         *dev,
                            struct udev_device *udev)
{
  BoltStatus after;
  BoltStatus before;

  before = bolt_device_get_status (dev);
  after = bolt_device_update_from_udev (dev, udev);

  bolt_info (LOG_DEV (dev), LOG_TOPIC ("udev"),
             "device changed: %s -> %s",
             bolt_status_to_string (before),
             bolt_status_to_string (after));

  /* reaction to status changes of the device, i.e.
   * authorizing the children are done in the
   * handle_device_status_changed () signal handler
   */
}

static void
handle_udev_device_removed (BoltManager *mgr,
                            BoltDevice  *dev)
{
  const char *syspath;

  syspath = bolt_device_get_syspath (dev);
  bolt_msg (LOG_DEV (dev), "removed (%s)", syspath);

  manager_deregister_device (mgr, dev);
}

static void
handle_udev_device_attached (BoltManager        *mgr,
                             BoltDomain         *domain,
                             BoltDevice         *dev,
                             struct udev_device *udev)
{
  g_autoptr(BoltDevice) parent = NULL;
  const char *syspath;
  BoltStatus status;

  syspath = udev_device_get_syspath (udev);
  status = bolt_device_connected (dev, domain, udev);

  bolt_msg (LOG_DEV (dev), "connected: %s (%s)",
            bolt_status_to_string (status), syspath);

  if (status != BOLT_STATUS_CONNECTED)
    return;

  parent = bolt_manager_get_parent (mgr, dev);
  if (parent)
    {
      const char *pid = bolt_device_get_uid (parent);
      status = bolt_device_get_status (parent);
      if (!bolt_status_is_authorized (status))
        {
          bolt_info (LOG_DEV (dev), "parent [%s] not authorized", pid);
          return;
        }
    }
  else
    {
      bolt_warn (LOG_DEV (dev), "could not find parent");
    }

  manager_auto_authorize (mgr, dev);
}

static void
handle_udev_device_detached (BoltManager *mgr,
                             BoltDevice  *dev)
{
  const char *syspath;

  syspath = bolt_device_get_syspath (dev);
  bolt_msg (LOG_DEV (dev), "disconnected (%s)", syspath);

  bolt_device_disconnected (dev);
}

static void
handle_store_device_added (BoltStore   *store,
                           const char  *uid,
                           BoltManager *mgr)
{
  g_autoptr(BoltDevice) dev = NULL;
  BoltDomain *dom = mgr->domains;

  dev = manager_find_device_by_uid (mgr, uid, NULL);

  if (dev == NULL || dom == NULL)
    return;

  if (bolt_device_get_policy (dev) != BOLT_POLICY_AUTO)
    {
      bolt_info (LOG_TOPIC ("bootacl"),
                 LOG_DEV_UID (uid),
                 "policy not 'auto', not adding");
      return;
    }

  bolt_domain_foreach (dom, bolt_bootacl_add, dev);
}

static void
handle_store_device_removed (BoltStore   *store,
                             const char  *uid,
                             BoltManager *mgr)
{
  g_autoptr(BoltDevice) dev = NULL;
  BoltDomain *dom = mgr->domains;
  BoltStatus status;

  dev = manager_find_device_by_uid (mgr, uid, NULL);

  if (!dev)
    return;

  bolt_msg (LOG_DEV (dev), "removed from store");

  /* TODO: maybe move to a new bolt_device_removed (dev) */
  g_object_set (dev,
                "store", NULL,
                "key", BOLT_KEY_MISSING,
                "policy", BOLT_POLICY_DEFAULT,
                NULL);

  /* remove device from bootacl */
  bolt_domain_foreach (dom, bolt_bootacl_del, dev);

  status = bolt_device_get_status (dev);
  /* if the device is connected, keep it around */
  if (status != BOLT_STATUS_DISCONNECTED)
    return;

  manager_deregister_device (mgr, dev);
}


static void
handle_domain_security_changed (BoltManager *mgr,
                                GParamSpec  *unused,
                                BoltDomain  *domain)
{
  BoltSecurity security = bolt_domain_get_security (domain);
  gboolean online = bolt_domain_is_connected (domain);

  if (online)
    manager_maybe_set_security (mgr, security);
}


static void
handle_device_status_changed (BoltDevice  *dev,
                              BoltStatus   old,
                              BoltManager *mgr)
{
  g_autoptr(GPtrArray) children = NULL;
  BoltStatus now;

  now = bolt_device_get_status (dev);
  bolt_debug (LOG_DEV (dev),
              "status changed: %s -> %s",
              bolt_status_to_string (old),
              bolt_status_to_string (now));

  if (now == old)
    return; /* sanity check */

  if (now == BOLT_STATUS_AUTHORIZING)
    mgr->authorizing += 1;
  else if (old == BOLT_STATUS_AUTHORIZING)
    mgr->authorizing -= 1;

  manager_probing_activity (mgr, !mgr->authorizing);

  if (now != BOLT_STATUS_AUTHORIZED)
    return;

  /* see if the new status changes anything for the
   * children, e.g. the can now be authorized */
  children = bolt_manager_get_children (mgr, dev);

  for (guint i = 0; i < children->len; i++)
    {
      BoltDevice *child = g_ptr_array_index (children, i);
      if (bolt_device_get_stored (child))
        manager_auto_authorize (mgr, child);
      else if (bolt_device_has_iommu (child))
        manager_auto_enroll (mgr, child);
    }
}

static void
handle_device_generation_changed (BoltDevice  *dev,
                                  GParamSpec  *unused,
                                  BoltManager *mgr)
{
  guint gen = bolt_device_get_generation (dev);

  bolt_debug (LOG_DEV (dev), LOG_TOPIC ("generation"),
              "updated to: %u", gen);

  if (!bolt_device_is_host (dev))
    return;

  manager_maybe_set_generation (mgr, gen);
}

static void
handle_power_state_changed (GObject    *gobject,
                            GParamSpec *pspec,
                            gpointer    user_data)
{
  BoltManager *mgr = BOLT_MANAGER (user_data);
  gboolean supported = bolt_power_can_force (mgr->power);
  BoltPowerState state = bolt_power_get_state (mgr->power);

  bolt_info (LOG_TOPIC ("power"), "state changed: %s/%s",
             (supported ? "supported" : "unsupported"),
             bolt_power_state_to_string (state));

  g_object_notify_by_pspec (G_OBJECT (mgr), props[PROP_POWERSTATE]);

  manager_sd_notify_status (mgr);
}


static gboolean
probing_timeout (gpointer user_data)
{
  BoltManager *mgr;
  gint64 now, dt, timeout;

  mgr = BOLT_MANAGER (user_data);

  if (mgr->authorizing > 0)
    return G_SOURCE_CONTINUE;

  now = g_get_monotonic_time ();
  dt = now - mgr->probing_tstamp;

  /* dt is in microseconds, probing timeout in
   * milli seconds  */
  timeout = mgr->probing_tsettle * BOLT_USEC_PER_MSEC;
  if (dt < timeout)
    return G_SOURCE_CONTINUE;

  /* we are done, remove us */
  mgr->probing_timeout = 0;
  g_object_notify_by_pspec (G_OBJECT (mgr), props[PROP_PROBING]);
  bolt_info (LOG_TOPIC ("probing"), "timeout, done: [%ld] (%ld)", dt, timeout);
  return G_SOURCE_REMOVE;
}

static void
manager_probing_activity (BoltManager *mgr,
                          gboolean     weak)
{
  guint dt;

  mgr->probing_tstamp = g_get_monotonic_time ();
  if (mgr->probing_timeout || weak)
    return;

  dt = mgr->probing_tsettle / 2;
  bolt_info (LOG_TOPIC ("probing"), "started [%u]", dt);
  mgr->probing_timeout = g_timeout_add (dt, probing_timeout, mgr);
  g_object_notify_by_pspec (G_OBJECT (mgr), props[PROP_PROBING]);
}

static gboolean
device_is_thunderbolt_root (struct udev_device *dev)
{
  const char *driver;
  const char *subsys;

  driver = udev_device_get_driver (dev);
  subsys = udev_device_get_subsystem (dev);

  return bolt_streq (subsys, "pci") &&
         bolt_streq (driver, "thunderbolt");
}

static gboolean
device_is_wakeup (struct udev_device *dev)
{
  const char *subsys;

  subsys = udev_device_get_subsystem (dev);

  return bolt_streq (subsys, "wakeup");
}

static gboolean
probing_add_root (BoltManager        *mgr,
                  struct udev_device *dev)
{
  const char *syspath;
  GPtrArray *roots;

  g_return_val_if_fail (device_is_thunderbolt_root (dev), FALSE);

  /* we go two levels up */
  for (guint i = 0; dev != NULL && i < 2; i++)
    dev = udev_device_get_parent (dev);

  if (dev == NULL)
    return FALSE;

  roots = mgr->probing_roots;
  syspath = udev_device_get_syspath (dev);
  g_ptr_array_add (roots, g_strdup (syspath));
  bolt_info (LOG_TOPIC ("probing"), "adding %s to roots", syspath);

  return TRUE;
}

static void
manager_probing_device_added (BoltManager        *mgr,
                              struct udev_device *dev)
{
  const char *syspath;
  GPtrArray *roots;
  gboolean added;

  syspath = udev_device_get_syspath (dev);

  if (syspath == NULL)
    return;

  /* ignore events for wakeup devices which get removed
   * and added at random time without any connection to
   * any physical thunderbolt device */
  if (device_is_wakeup (dev))
    return;

  roots = mgr->probing_roots;
  for (guint i = 0; i < roots->len; i++)
    {
      const char *r = g_ptr_array_index (roots, i);
      if (g_str_has_prefix (syspath, r))
        {
          bolt_debug (LOG_TOPIC ("probing"), "match %s", syspath);
          /* do something */
          manager_probing_activity (mgr, FALSE);
          return;
        }
    }

  /* if we ended up here we didn't find a root,
   * maybe we are one
   */
  if (!device_is_thunderbolt_root (dev))
    return;

  added = probing_add_root (mgr, dev);
  if (added)
    manager_probing_activity (mgr, FALSE);
}

static void
manager_probing_device_removed (BoltManager        *mgr,
                                struct udev_device *dev)
{
  const char *syspath;
  GPtrArray *roots;
  gboolean found;
  guint index;

  syspath = udev_device_get_syspath (dev);

  if (syspath == NULL)
    return;

  roots = mgr->probing_roots;
  found = FALSE;
  for (index = 0; index < roots->len; index++)
    {
      const char *r = g_ptr_array_index (roots, index);
      found = g_str_equal (syspath, r);
      if (found)
        break;
    }

  if (!found)
    return;

  bolt_info (LOG_TOPIC ("probing"), "removing %s from roots", syspath);
  g_ptr_array_remove_index_fast (mgr->probing_roots, index);
}

static void
manager_probing_domain_added (BoltManager        *mgr,
                              struct udev_device *domain)
{
  struct udev_device *p = domain;

  /* walk up until we find the thunderbolt root */
  while (p && !device_is_thunderbolt_root (p))
    p = udev_device_get_parent (p);

  if (p == NULL)
    return;

  probing_add_root (mgr, p);
}

static BoltGuard *
manager_maybe_power_controller (BoltManager *mgr)
{
  g_autoptr(GError) err = NULL;
  BoltGuard *guard = NULL;
  gboolean can_force_power;
  int n;

  can_force_power = bolt_power_can_force (mgr->power);

  if (can_force_power == FALSE)
    return NULL;

  n = bolt_udev_count_hosts (mgr->udev, &err);
  if (n < 0)
    {
      bolt_warn_err (err, LOG_TOPIC ("udev"),
                     "failed to count domains");
      return NULL;
    }
  else if (n > 0)
    {
      goto out;
    }

  guard = bolt_power_acquire (mgr->power, &err);

  if (guard == NULL)
    {
      bolt_warn_err (err, LOG_TOPIC ("power"),
                     "could not force power");
      return NULL;
    }

  /* we wait for a total of 5.0 seconds, should hopefully
   * be enough for at least the domain to show up. */
  for (int i = 0; i < 25 && n < 1; i++)
    {
      g_usleep (200000); /* 200 000 us = 0.2s */
      n = bolt_udev_count_hosts (mgr->udev, NULL);
    }

out:
  bolt_info (LOG_TOPIC ("udev"), "found %d domain%s",
             n, n > 1 ? "s" : "");
  return guard;
}


/* config */
static void
manager_load_user_config (BoltManager *mgr)
{
  g_autoptr(GError) err = NULL;
  BoltPolicy policy;
  BoltAuthMode authmode;
  BoltTri res;

  bolt_info (LOG_TOPIC ("config"), "loading user config");
  mgr->config = bolt_store_config_load (mgr->store, &err);
  if (mgr->config == NULL)
    {
      if (!bolt_err_notfound (err))
        bolt_warn_err (err, LOG_TOPIC ("config"),
                       "failed to load user config");
      return;
    }

  bolt_info (LOG_TOPIC ("config"), "user config loaded successfully");
  res = bolt_config_load_default_policy (mgr->config, &policy, &err);
  if (res == TRI_ERROR)
    {
      bolt_warn_err (err, LOG_TOPIC ("config"),
                     "failed to load default policy");
      g_clear_error (&err);
    }
  else if (res == TRI_YES)
    {
      mgr->policy = policy;
      bolt_info (LOG_TOPIC ("config"), "default policy set to %s",
                 bolt_policy_to_string (policy));
      g_object_notify_by_pspec (G_OBJECT (mgr), props[PROP_POLICY]);
    }

  res = bolt_config_load_auth_mode (mgr->config, &authmode, &err);
  if (res == TRI_ERROR)
    {
      bolt_warn_err (err, LOG_TOPIC ("config"),
                     "failed to load auth mode");
      g_clear_error (&err);
    }
  else if (res == TRI_YES)
    {
      g_autofree char *str = NULL;

      str = bolt_flags_to_string (BOLT_TYPE_AUTH_MODE, authmode, NULL);
      bolt_info (LOG_TOPIC ("config"), "auth mode set to '%s'", str);
      mgr->authmode = authmode;
      g_object_notify_by_pspec (G_OBJECT (mgr), props[PROP_POLICY]);
    }
}

/* dbus property setter */
static gboolean
handle_set_authmode (BoltExported *obj,
                     const char   *name,
                     const GValue *value,
                     GError      **error)
{
  g_autoptr(GError) err = NULL;
  g_autofree char *str = NULL;
  BoltManager *mgr = BOLT_MANAGER (obj);
  BoltAuthMode authmode;
  gboolean ok;

  authmode = g_value_get_flags (value);

  if (authmode == mgr->authmode)
    return TRUE;

  if (mgr->config == NULL)
    mgr->config = bolt_config_user_init ();

  str = bolt_flags_to_string (BOLT_TYPE_AUTH_MODE, authmode, &err);
  if (str == NULL)
    {
      bolt_warn_err (err, LOG_TOPIC ("config"), "error setting authmode");
      return bolt_error_propagate (error, &err);
    }

  bolt_config_set_auth_mode (mgr->config, str);
  ok = bolt_store_config_save (mgr->store, mgr->config, &err);

  if (!ok)
    {
      bolt_warn_err (err, LOG_TOPIC ("config"), "error saving config");
      return bolt_error_propagate (error, &err);
    }

  mgr->authmode = authmode;
  bolt_info (LOG_TOPIC ("config"), "auth mode set to '%s'", str);

  manager_sd_notify_status (mgr);

  return ok;
}

/* dbus methods: domain related */
static GVariant *
handle_list_domains (BoltExported          *object,
                     GVariant              *params,
                     GDBusMethodInvocation *invocation,
                     GError               **error)
{
  BoltManager *mgr = BOLT_MANAGER (object);
  const char **domains;
  BoltDomain *iter;
  guint count;

  count = bolt_domain_count (mgr->domains);
  domains = g_newa (const char *, count + 1);

  iter = mgr->domains;
  for (guint i = 0; i < count; i++)
    {
      domains[i] = bolt_exported_get_object_path (BOLT_EXPORTED (iter));
      iter = bolt_domain_next (iter);
    }

  domains[count] = NULL;

  return g_variant_new ("(^ao)", domains);
}

static GVariant *
handle_domain_by_id (BoltExported          *object,
                     GVariant              *params,
                     GDBusMethodInvocation *invocation,
                     GError               **error)
{
  BoltManager *mgr = BOLT_MANAGER (object);
  BoltDomain *domain;
  const char *op;
  const char *id;

  g_variant_get (params, "(&s)", &id);

  if (id == NULL || id[0] == '\0')
    {
      g_set_error_literal (error, G_IO_ERROR,
                           G_IO_ERROR_INVALID_ARGUMENT,
                           "empty domain id");
      return NULL;
    }

  domain = bolt_domain_find_id (mgr->domains, id, error);

  if (domain == NULL)
    return NULL;

  op = bolt_exported_get_object_path (BOLT_EXPORTED (domain));
  return g_variant_new ("(o)", op);
}

/* dbus methods: device related */
static GVariant *
handle_list_devices (BoltExported          *obj,
                     GVariant              *params,
                     GDBusMethodInvocation *inv,
                     GError               **error)
{
  BoltManager *mgr = BOLT_MANAGER (obj);
  const char **devs;

  devs = g_newa (const char *, mgr->devices->len + 1);

  for (guint i = 0; i < mgr->devices->len; i++)
    {
      BoltDevice *d = g_ptr_array_index (mgr->devices, i);
      devs[i] = bolt_device_get_object_path (d);
    }

  devs[mgr->devices->len] = NULL;

  return g_variant_new ("(^ao)", devs);
}

static GVariant *
handle_device_by_uid (BoltExported          *obj,
                      GVariant              *params,
                      GDBusMethodInvocation *inv,
                      GError               **error)
{
  g_autoptr(BoltDevice) dev = NULL;
  BoltManager *mgr;
  const char *uid;
  const char *opath;

  mgr = BOLT_MANAGER (obj);

  g_variant_get (params, "(&s)", &uid);
  dev = manager_find_device_by_uid (mgr, uid, error);

  if (dev == NULL)
    return NULL;

  opath = bolt_device_get_object_path (dev);
  return g_variant_new ("(o)", opath);
}

static void
enroll_device_done (GObject      *device,
                    GAsyncResult *res,
                    gpointer      user_data)
{
  GDBusMethodInvocation *inv = user_data;
  BoltDevice *dev = BOLT_DEVICE (device);
  BoltAuth *auth = BOLT_AUTH (res);
  GError *error = NULL;
  BoltManager *mgr;
  const char *opath;
  gboolean ok;

  mgr = BOLT_MANAGER (bolt_auth_get_origin (auth));
  ok = bolt_auth_check (auth, &error);

  if (ok)
    {
      ok = bolt_store_put_device (mgr->store,
                                  dev,
                                  bolt_auth_get_policy (auth),
                                  bolt_auth_get_key (auth),
                                  &error);
    }

  if (!ok)
    {
      g_dbus_method_invocation_take_error (inv, error);
      return;
    }

  opath = bolt_device_get_object_path (dev);
  g_dbus_method_invocation_return_value (inv, g_variant_new ("(o)", opath));
}

static GVariant *
enroll_device_store_authorized (BoltManager *mgr,
                                BoltDevice  *dev,
                                BoltPolicy   policy,
                                GError     **error)
{
  g_autoptr(GError) err = NULL;
  g_autoptr(BoltKey) key = NULL;
  const char *opath;
  gboolean ok;

  bolt_info (LOG_DEV (dev), "enrolling an authorized device (%s)",
             bolt_policy_to_string (policy));

  ok = bolt_device_get_key_from_sysfs (dev, &key, &err);
  if (!ok)
    {
      bolt_warn_err (err, LOG_DEV (dev), LOG_TOPIC ("udev"),
                     "failed to read key from sysfs");
      g_propagate_prefixed_error (error, g_steal_pointer (&err), "%s",
                                  "could not determine existing authorization: ");
      return NULL;
    }

  ok = bolt_store_put_device (mgr->store, dev, policy, key, &err);

  if (!ok)
    {
      bolt_warn_err (err, LOG_DEV (dev), LOG_TOPIC ("store"),
                     "failed to store device");
      bolt_error_propagate (error, &err);
      return NULL;
    }

  opath = bolt_device_get_object_path (dev);
  return g_variant_new ("(o)", opath);
}

static GVariant *
handle_enroll_device (BoltExported          *obj,
                      GVariant              *params,
                      GDBusMethodInvocation *inv,
                      GError               **error)
{
  g_autoptr(BoltDevice) dev = NULL;
  g_autoptr(BoltAuth) auth = NULL;
  BoltManager *mgr;
  const char *uid;
  BoltPolicy pol;
  const char *policy;

  mgr = BOLT_MANAGER (obj);

  g_variant_get_child (params, 0, "&s", &uid);
  g_variant_get_child (params, 1, "&s", &policy);
  dev = manager_find_device_by_uid (mgr, uid, error);

  if (dev == NULL)
    return NULL;

  pol = bolt_enum_from_string (BOLT_TYPE_POLICY, policy, error);
  if (pol == BOLT_POLICY_UNKNOWN)
    {
      if (*error == NULL)
        g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                     "invalid policy: %s", policy);
      return NULL;
    }
  else if (pol == BOLT_POLICY_DEFAULT)
    {
      if (bolt_device_has_iommu (dev))
        pol = BOLT_POLICY_IOMMU;
      else
        pol = mgr->policy;

      bolt_info (LOG_DEV (dev), LOG_TOPIC ("enroll"),
                 "got 'default' policy, adjusted to: '%s'",
                 bolt_policy_to_string (pol));
    }

  if (bolt_device_get_stored (dev))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS,
                   "device with id '%s' already enrolled.",
                   uid);
      return NULL;
    }

  /* if the device is already authorized, we just store it */
  if (bolt_device_is_authorized (dev))
    return enroll_device_store_authorized (mgr, dev, pol, error);

  auth = manager_enroll_device_prepare (mgr, dev, error);

  if (auth == NULL)
    return NULL;

  bolt_auth_set_policy (auth, pol);
  bolt_device_authorize (dev, auth, enroll_device_done, inv);
  return NULL;

}

static GVariant *
handle_forget_device (BoltExported          *obj,
                      GVariant              *params,
                      GDBusMethodInvocation *inv,
                      GError               **error)
{
  g_autoptr(BoltDevice) dev = NULL;
  BoltManager *mgr;
  gboolean ok;
  const char *uid;

  mgr = BOLT_MANAGER (obj);

  g_variant_get (params, "(&s)", &uid);
  dev = manager_find_device_by_uid (mgr, uid, error);

  if (dev == NULL)
    return FALSE;

  ok = bolt_store_del (mgr->store, dev, error);

  return ok ? g_variant_new ("()") : NULL;
}

/* public methods */
gboolean
bolt_manager_export (BoltManager     *mgr,
                     GDBusConnection *connection,
                     GError         **error)
{
  g_autoptr(GError) err  = NULL;
  gboolean ok;

  if (!bolt_exported_export (BOLT_EXPORTED (mgr),
                             connection,
                             BOLT_DBUS_PATH,
                             error))
    return FALSE;

  ok = bolt_exported_export (BOLT_EXPORTED (mgr->power),
                             connection,
                             BOLT_DBUS_PATH,
                             error);

  if (!ok)
    {
      bolt_warn_err (err, LOG_TOPIC ("dbus"),
                     "failed to export power object");
      g_clear_error (&err);
    }

  bolt_domain_foreach (mgr->domains,
                       (GFunc) bolt_domain_export,
                       connection);

  for (guint i = 0; i < mgr->devices->len; i++)
    {

      BoltDevice *dev = g_ptr_array_index (mgr->devices, i);
      const char *opath;

      opath = bolt_device_export (dev, connection, &err);
      if (opath == NULL)
        {
          bolt_warn_err (err, LOG_DEV (dev), LOG_TOPIC ("dbus"),
                         "error exporting a device");
          g_clear_error (&err);
          continue;
        }

      bolt_info (LOG_DEV (dev), LOG_TOPIC ("dbus"),
                 "exported device at %.43s...", opath);
    }

  return TRUE;
}

void
bolt_manager_got_the_name (BoltManager *mgr)
{

  /* emit DeviceAdded signals now that we have the name
   * for all devices that are not stored and connected
   */
  for (guint i = 0; i < mgr->devices->len; i++)
    {
      BoltDevice *dev = g_ptr_array_index (mgr->devices, i);
      BoltStatus status;
      gboolean stored;
      const char *opath;

      stored = bolt_device_get_stored (dev);
      if (stored)
        continue;

      status = bolt_device_get_status (dev);
      if (status != BOLT_STATUS_CONNECTED)
        continue;

      opath = bolt_exported_get_object_path (BOLT_EXPORTED (dev));
      if (opath == NULL)
        continue;

      bolt_exported_emit_signal (BOLT_EXPORTED (mgr),
                                 "DeviceAdded",
                                 g_variant_new ("(o)", opath),
                                 NULL);
    }
}

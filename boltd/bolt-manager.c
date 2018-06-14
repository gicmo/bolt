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

#include "bolt-manager.h"

#include <libudev.h>
#include <string.h>

#define MSEC_PER_USEC 1000LL
#define PROBING_SETTLE_TIME_MS 2000 /* in milli-seconds */

typedef struct udev_monitor udev_monitor;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (udev_monitor, udev_monitor_unref);

typedef struct udev_device udev_device;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (udev_device, udev_device_unref);


static void     bolt_manager_initable_iface_init (GInitableIface *iface);


static gboolean bolt_manager_initialize (GInitable    *initable,
                                         GCancellable *cancellable,
                                         GError      **error);
/* domain related functions */
static BoltDomain *  manager_find_domain_by_syspath (BoltManager *mgr,
                                                     const char  *syspath);

static void          manager_register_domain (BoltManager *mgr,
                                              BoltDomain  *domain);

static void          manager_deregister_domain (BoltManager *mgr,
                                                BoltDomain  *domain);

/* device related functions */
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
static gboolean      handle_uevent_udev (GIOChannel  *source,
                                         GIOCondition condition,
                                         gpointer     user_data);

static void          handle_udev_domain_event (BoltManager        *mgr,
                                               struct udev_device *device,
                                               const char         *action);

static void          handle_udev_domain_added (BoltManager        *mgr,
                                               struct udev_device *udev);

static void          handle_udev_domain_removed (BoltManager *mgr,
                                                 BoltDomain  *domain);

static void          handle_udev_device_event (BoltManager        *mgr,
                                               struct udev_device *device,
                                               const char         *action);

static void          handle_udev_device_added (BoltManager        *mgr,
                                               struct udev_device *udev);

static void          handle_udev_device_changed (BoltManager        *mgr,
                                                 BoltDevice         *dev,
                                                 struct udev_device *udev);

static void          hanlde_udev_device_removed (BoltManager *mgr,
                                                 BoltDevice  *dev);

static void          handle_udev_device_attached (BoltManager        *mgr,
                                                  BoltDevice         *dev,
                                                  struct udev_device *udev);

static void          handle_udev_device_detached (BoltManager *mgr,
                                                  BoltDevice  *dev);

/* signal callbacks */
static void          handle_store_device_removed (BoltStore   *store,
                                                  const char  *uid,
                                                  BoltManager *mgr);

static void          handle_device_status_changed (BoltDevice  *dev,
                                                   BoltStatus   old,
                                                   BoltManager *mgr);

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
static gboolean      manager_maybe_power_controller (BoltManager *mgr);

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
  struct udev         *udev;
  struct udev_monitor *udev_monitor;
  GSource             *udev_source;

  /* state */
  BoltStore   *store;
  BoltDomain  *domains;
  GPtrArray   *devices;
  BoltPower   *power;
  BoltSecurity security;
  BoltAuthMode authmode;

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
};

enum {
  PROP_0,

  PROP_VERSION,
  PROP_PROBING,
  PROP_POLICY,
  PROP_SECURITY,
  PROP_AUTHMODE,

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

  if (mgr->udev_monitor)
    {
      udev_monitor_unref (mgr->udev_monitor);
      mgr->udev_monitor = NULL;

      g_source_destroy (mgr->udev_source);
      g_source_unref (mgr->udev_source);
      mgr->udev_source = NULL;
    }

  if (mgr->udev)
    {
      udev_unref (mgr->udev);
      mgr->udev = NULL;
    }

  if (mgr->probing_timeout)
    {
      g_source_remove (mgr->probing_timeout);
      mgr->probing_timeout = 0;
    }

  g_clear_object (&mgr->store);
  g_ptr_array_free (mgr->devices, TRUE);
  bolt_domain_clear (&mgr->domains);

  g_clear_object (&mgr->power);

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

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bolt_manager_init (BoltManager *mgr)
{
  mgr->devices = g_ptr_array_new_with_free_func (g_object_unref);
  mgr->store = bolt_store_new (g_getenv ("BOLT_DBPATH") ? : BOLT_DBDIR);

  mgr->probing_roots = g_ptr_array_new_with_free_func (g_free);
  mgr->probing_tsettle = PROBING_SETTLE_TIME_MS; /* milliseconds */

  mgr->security = BOLT_SECURITY_UNKNOWN;

  /* default configuration */
  mgr->policy = BOLT_POLICY_AUTO;
  mgr->authmode = BOLT_AUTH_ENABLED;

  g_signal_connect_object (mgr->store, "device-removed",
                           G_CALLBACK (handle_store_device_removed),
                           mgr, 0);
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

  g_object_class_install_properties (gobject_class, PROP_LAST, props);


  bolt_exported_class_set_interface_info (exported_class,
                                          BOLT_DBUS_INTERFACE,
                                          "/boltd/org.freedesktop.bolt.xml");

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
monitor_add_filter (struct udev_monitor *monitor,
                    const char          *subsystem_devtype,
                    GError             **error)
{
  g_autofree char *subsystem = NULL;
  char *devtype = NULL;
  gboolean ok;
  int r;

  subsystem = g_strdup (subsystem_devtype);

  devtype = strchr (subsystem, '/');
  if (devtype != NULL)
    *devtype++ = '\0';

  r = udev_monitor_filter_add_match_subsystem_devtype (monitor,
                                                       subsystem,
                                                       devtype);
  ok = r > -1;
  if (!ok)
    g_set_error (error, BOLT_ERROR, BOLT_ERROR_UDEV,
                 "udev: could not add match for '%s' (%s) to monitor",
                 subsystem, devtype ? : "*");

  return ok;
}

static gboolean
setup_monitor (BoltManager        *mgr,
               const char         *name,
               const char * const *filter,
               GSourceFunc         callback,
               udev_monitor      **monitor_out,
               GSource           **watch_out,
               GError            **error)
{
  g_autoptr(udev_monitor) monitor = NULL;
  g_autoptr(GIOChannel) channel = NULL;
  GSource *watch;
  gboolean ok;
  int fd;
  int res;

  monitor = udev_monitor_new_from_netlink (mgr->udev, name);
  if (monitor == NULL)
    {
      g_set_error_literal (error, BOLT_ERROR, BOLT_ERROR_UDEV,
                           "udev: could not create monitor");
      return FALSE;
    }

  udev_monitor_set_receive_buffer_size (monitor, 128 * 1024 * 1024);

  for (guint i = 0; filter && filter[i] != NULL; i++)
    {
      ok = monitor_add_filter (monitor, filter[i], error);
      if (!ok)
        return FALSE;
    }

  res = udev_monitor_enable_receiving (monitor);
  if (res < 0)
    {
      g_set_error_literal (error, BOLT_ERROR, BOLT_ERROR_UDEV,
                           "udev: could not enable monitoring");
      return FALSE;
    }

  fd = udev_monitor_get_fd (monitor);

  if (fd < 0)
    {
      g_set_error_literal (error, BOLT_ERROR, BOLT_ERROR_UDEV,
                           "udev: could not obtain fd for monitoring");
      return FALSE;
    }

  channel = g_io_channel_unix_new (fd);
  watch   = g_io_create_watch (channel, G_IO_IN);

  g_source_set_callback (watch, callback, mgr, NULL);
  g_source_attach (watch, g_main_context_get_thread_default ());

  *monitor_out = udev_monitor_ref (monitor);
  *watch_out   = watch;

  return TRUE;
}

static gboolean
bolt_manager_initialize (GInitable    *initable,
                         GCancellable *cancellable,
                         GError      **error)
{
  g_auto(GStrv) ids = NULL;
  BoltManager *mgr;
  struct udev_enumerate *enumerate;
  struct udev_list_entry *l, *devices;
  gboolean forced_power;
  gboolean ok;

  mgr = BOLT_MANAGER (initable);

  /* load dynamic user configuration */
  manager_load_user_config (mgr);

  /* polkit setup */
  mgr->bouncer = bolt_bouncer_new (cancellable, error);
  if (mgr->bouncer == NULL)
    return FALSE;

  bolt_bouncer_add_client (mgr->bouncer, mgr);

  /* udev setup*/
  mgr->udev = udev_new ();
  if (mgr->udev == NULL)
    {
      g_set_error_literal (error, BOLT_ERROR, BOLT_ERROR_UDEV,
                           "udev: could not create udev handle");
      return FALSE;
    }

  ok = setup_monitor (mgr, "udev",
                      NULL,
                      (GSourceFunc) handle_uevent_udev,
                      &mgr->udev_monitor, &mgr->udev_source,
                      error);

  if (!ok)
    return FALSE;

  ids = bolt_store_list_uids (mgr->store, error);
  if (ids == NULL)
    {
      g_prefix_error (error, "failed to list devices in store");
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

  mgr->power = bolt_power_new (mgr->udev);
  forced_power = manager_maybe_power_controller (mgr);

  /* TODO: error checking */
  enumerate = udev_enumerate_new (mgr->udev);
  udev_enumerate_add_match_subsystem (enumerate, "thunderbolt");
  /* only devices (i.e. not the domain controller) */

  bolt_info (LOG_TOPIC ("udev"), "enumerating devices");
  udev_enumerate_scan_devices (enumerate);
  devices = udev_enumerate_get_list_entry (enumerate);

  udev_list_entry_foreach (l, devices)
    {
      g_autoptr(udev_device) udevice = NULL;
      g_autoptr(BoltDevice) dev = NULL;
      const char *uid;
      const char *syspath;
      const char *devtype;

      syspath = udev_list_entry_get_name (l);
      udevice = udev_device_new_from_syspath (mgr->udev, syspath);

      if (udevice == NULL)
        continue;

      devtype = udev_device_get_devtype (udevice);

      if (bolt_streq (devtype, "thunderbolt_domain"))
        handle_udev_domain_added (mgr, udevice);

      if (!bolt_streq (devtype, "thunderbolt_device"))
        continue;

      uid = udev_device_get_sysattr_value (udevice, "unique_id");
      if (uid == NULL)
        {
          bolt_warn ("thunderbolt device without uid");
          continue;
        }

      dev = manager_find_device_by_uid (mgr, uid, NULL);
      if (dev)
        handle_udev_device_attached (mgr, dev, udevice);
      else
        handle_udev_device_added (mgr, udevice);
    }

  udev_enumerate_unref (enumerate);

  if (forced_power)
    {
      g_autoptr(GError) err = NULL;

      ok = bolt_power_force_switch (mgr->power, FALSE, &err);

      if (!ok)
        bolt_warn_err (err, LOG_TOPIC ("power"), "failed undo force power");
      else
        bolt_info (LOG_TOPIC ("power"), "setting force_power to OFF");
    }

  return TRUE;
}

/* domain related function */
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
      if (g_str_has_prefix (syspath, prefix))
        return iter;

      iter = bolt_domain_next (iter);
    }

  return NULL;
}

static void
manager_register_domain (BoltManager *mgr,
                         BoltDomain  *domain)
{
  const char *name;
  BoltSecurity sl;

  mgr->domains = bolt_domain_insert (mgr->domains, domain);

  name = bolt_domain_get_id (domain);
  sl = bolt_domain_get_security (domain);

  bolt_info (LOG_TOPIC ("domain"), "'%s' (security: %s) added",
             name, bolt_security_to_string (sl));

  if (mgr->security == BOLT_SECURITY_UNKNOWN)
    {
      bolt_info ("security level set to '%s'",
                 bolt_security_to_string (sl));
      mgr->security = sl;
    }
  else if (mgr->security != sl)
    {
      bolt_warn ("multiple security levels (%s vs %s)",
                 bolt_security_to_string (mgr->security),
                 bolt_security_to_string (sl));
    }
}

static void
manager_deregister_domain (BoltManager *mgr,
                           BoltDomain  *domain)
{
  const char *name;

  name = bolt_domain_get_id (domain);
  bolt_info (LOG_TOPIC ("domain"), "'%s' removed", name);

  mgr->domains = bolt_domain_remove (mgr->domains, domain);
}

/* device related functions */
static void
manager_register_device (BoltManager *mgr,
                         BoltDevice  *dev)
{

  g_ptr_array_add (mgr->devices, dev);
  bolt_bouncer_add_client (mgr->bouncer, dev);
  g_signal_connect_object (dev, "status-changed",
                           G_CALLBACK (handle_device_status_changed),
                           mgr, 0);
}

static void
manager_deregister_device (BoltManager *mgr,
                           BoltDevice  *dev)
{
  g_ptr_array_remove_fast (mgr->devices, dev);
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

  name = bolt_device_get_name (target);
  vendor = bolt_device_get_vendor (target);

  for (guint i = 0; i < mgr->devices->len; i++)
    {
      BoltDevice *dev = g_ptr_array_index (mgr->devices, i);
      const char *dev_name = bolt_device_get_name (dev);
      const char *dev_vendor = bolt_device_get_vendor (dev);

      if (bolt_streq (dev_name, name) &&
          bolt_streq (dev_vendor, vendor))
        count++;
    }

  /* cleanup name */
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
authorize_device_finish (GObject      *source,
                         GAsyncResult *res,
                         gpointer      user_data)
{
  g_autoptr(GError) err = NULL;
  BoltDevice *dev = BOLT_DEVICE (source);
  BoltAuth *auth = BOLT_AUTH (res);
  gboolean ok;

  ok = bolt_auth_check (auth, &err);

  if (!ok)
    bolt_warn_err (err, LOG_DEV (dev), "authorization failed");
  else
    bolt_msg (LOG_DEV (dev), "authorized");
}

static void
maybe_authorize_device (BoltManager *mgr,
                        BoltDevice  *dev)
{
  g_autoptr(BoltAuth) auth = NULL;
  BoltStatus status = bolt_device_get_status (dev);
  BoltPolicy policy = bolt_device_get_policy (dev);
  const char *uid = bolt_device_get_uid (dev);
  BoltKey *key = NULL;
  BoltSecurity level;
  gboolean stored;

  bolt_info (LOG_DEV (dev), "checking possible authorization: %s (%x)",
             bolt_policy_to_string (policy), status);

  if (bolt_auth_mode_is_disabled (mgr->authmode))
    {
      bolt_info (LOG_DEV (dev), "authorization is globally disabled.");
      return;
    }

  if (bolt_status_is_authorized (status) ||
      policy != BOLT_POLICY_AUTO)
    return;

  stored = bolt_device_get_stored (dev);
  /* sanity check, because we already checked the policy */
  g_return_if_fail (stored);

  level = bolt_device_get_security (dev);
  if (level == BOLT_SECURITY_SECURE &&
      bolt_device_get_keystate (dev) != BOLT_KEY_MISSING)
    {
      g_autoptr(GError) err = NULL;
      key = bolt_store_get_key (mgr->store, uid, &err);
      if (key == NULL)
        bolt_warn_err (err, LOG_DEV (dev), "could not load key");
    }

  if (level == BOLT_SECURITY_SECURE && key == NULL)
    {
      bolt_msg (LOG_DEV (dev), "have no key, not authorizing (secure mode)");
      return;
    }

  auth = bolt_auth_new (mgr, level, key);
  bolt_device_authorize_idle (dev, auth, authorize_device_finish, mgr);
}

static void
manager_maybe_auto_import_device (BoltManager *mgr,
                                  BoltDevice  *dev)
{
  g_autoptr(GError) err = NULL;
  g_autoptr(BoltKey) key = NULL;
  gboolean boot;
  gboolean ok;

  if (!bolt_device_is_authorized (dev))
    return;

  if (bolt_device_get_device_type (dev) == BOLT_DEVICE_HOST)
    return;

  boot = bolt_device_check_authflag (dev, BOLT_AUTH_BOOT);

  if (!boot)
    {
      bolt_msg (LOG_DEV (dev), LOG_TOPIC ("auto-import"),
                "boot flag missing, not auto-importing");
      return;
    }

  key = bolt_device_get_key_from_sysfs (dev, &err);
  if (key == NULL && !bolt_err_notfound (err))
    {
      bolt_warn_err (err, LOG_DEV (dev), LOG_TOPIC ("auto-import"),
                     "failed to read key from sysfs");
      return;
    }

  if (key == NULL && mgr->security == BOLT_SECURITY_SECURE)
    {
      bolt_msg (LOG_DEV (dev), LOG_TOPIC ("auto-import"),
                "secure mode enabled, no key, not auto-importing");
      return;
    }

  bolt_msg (LOG_DEV (dev), LOG_TOPIC ("auto-import"),
            "new authorized device (boot: %s, key: %s), importing",
            bolt_yesno (boot), bolt_yesno (key != NULL));

  ok = bolt_store_put_device (mgr->store, dev, BOLT_POLICY_AUTO, key, &err);

  if (!ok)
    bolt_warn_err (err, LOG_DEV (dev), LOG_TOPIC ("auto-import"),
                   "failed to store device");
}

/* udev callbacks */
static gboolean
handle_uevent_udev (GIOChannel  *source,
                    GIOCondition condition,
                    gpointer     user_data)
{
  g_autoptr(udev_device) device = NULL;
  BoltManager *mgr;
  const char *action;
  const char *subsystem;
  const char *devtype;
  const char *syspath;

  mgr = BOLT_MANAGER (user_data);
  device = udev_monitor_receive_device (mgr->udev_monitor);

  if (device == NULL)
    return G_SOURCE_CONTINUE;

  action = udev_device_get_action (device);
  if (action == NULL)
    return G_SOURCE_CONTINUE;

  syspath = udev_device_get_syspath (device);
  if (syspath == NULL)
    return G_SOURCE_CONTINUE;

  devtype = udev_device_get_devtype (device);
  subsystem = udev_device_get_subsystem (device);

  if (g_str_equal (action, "add"))
    manager_probing_device_added (mgr, device);
  else if (g_str_equal (action, "remove"))
    manager_probing_device_removed (mgr, device);

  /* beyond this point only udev device from the
   * thunderbolt are handled */
  if (!bolt_streq (subsystem, "thunderbolt"))
    return G_SOURCE_CONTINUE;

  bolt_debug (LOG_TOPIC ("udev"), "%s (%s%s%s) %s", action,
              subsystem, devtype ? "/" : "", devtype ? : "",
              syspath);

  if (bolt_streq (devtype, "thunderbolt_device"))
    handle_udev_device_event (mgr, device, action);
  else if (bolt_streq (devtype, "thunderbolt_domain"))
    handle_udev_domain_event (mgr, device, action);

  return G_SOURCE_CONTINUE;
}

static void
handle_udev_domain_event (BoltManager        *mgr,
                          struct udev_device *device,
                          const char         *action)
{
  const char *syspath;
  BoltDomain *domain;

  syspath = udev_device_get_syspath (device);

  if (g_str_equal (action, "add") ||
      g_str_equal (action, "change"))
    {
      domain = manager_find_domain_by_syspath (mgr, syspath);

      if (domain != NULL)
        return; /*change event, ignore for now */

      handle_udev_domain_added (mgr, device);
    }
  else if (g_str_equal (action, "remove"))
    {
      domain = manager_find_domain_by_syspath (mgr, syspath);

      if (domain)
        handle_udev_domain_removed (mgr, domain);
      else
        bolt_warn (LOG_TOPIC ("domain"),
                   "unregistered domain removed at %s",
                   syspath);
    }
}

static void
handle_udev_domain_added (BoltManager        *mgr,
                          struct udev_device *device)
{
  g_autoptr(GError) err = NULL;
  g_autoptr(BoltDomain) domain = NULL;
  GDBusConnection *bus;
  const char *op;

  manager_probing_domain_added (mgr, device);

  domain = bolt_domain_new_for_udev (device, &err);

  if (domain == NULL)
    {
      bolt_warn_err (err, LOG_TOPIC ("udev"),
                     "failed to create domain: %s");
      return;
    }

  manager_register_domain (mgr, domain);

  bus = bolt_exported_get_connection (BOLT_EXPORTED (mgr));
  if (bus == NULL)
    return;

  bolt_domain_export (domain, bus);

  op = bolt_exported_get_object_path (BOLT_EXPORTED (domain));
  bolt_exported_emit_signal (BOLT_EXPORTED (mgr),
                             "DomainAdded",
                             g_variant_new ("(o)", op),
                             NULL);
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

  if (g_str_equal (action, "add") ||
      g_str_equal (action, "change"))
    {
      const char *uid;

      /* filter sysfs devices (e.g. the domain) that don't have
       * the unique_id attribute */
      uid = udev_device_get_sysattr_value (device, "unique_id");
      if (uid == NULL)
        return;

      dev = manager_find_device_by_uid (mgr, uid, NULL);

      if (!dev)
        handle_udev_device_added (mgr, device);
      else if (!bolt_device_is_connected (dev))
        handle_udev_device_attached (mgr, dev, device);
      else
        handle_udev_device_changed (mgr, dev, device);
    }
  else if (g_str_equal (action, "remove"))
    {
      const char *syspath;
      const char *name;

      syspath = udev_device_get_syspath (device);
      if (syspath == NULL)
        {
          bolt_warn (LOG_TOPIC ("udev"), "device without syspath");
          return;
        }

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
        hanlde_udev_device_removed (mgr, dev);
    }
}

static void
handle_udev_device_added (BoltManager        *mgr,
                          struct udev_device *udev)
{
  g_autoptr(GError) err = NULL;
  GDBusConnection *bus;
  BoltDevice *dev;
  BoltStatus status;
  BoltDomain *domain;
  const char *opath;
  const char *syspath;

  syspath = udev_device_get_syspath (udev);
  domain = manager_find_domain_by_syspath (mgr, syspath);

  if (domain == NULL)
    {
      bolt_warn (LOG_TOPIC ("domain"),
                 "could not find domain for device at '%s'",
                 syspath);
      return;
    }

  dev = bolt_device_new_for_udev (udev, domain, &err);
  if (dev == NULL)
    {
      bolt_warn_err (err, LOG_TOPIC ("udev"), "could not create device");
      return;
    }

  manager_register_device (mgr, dev);

  status = bolt_device_get_status (dev);
  bolt_msg (LOG_DEV (dev), "device added, status: %s, at %s",
            bolt_status_to_string (status), syspath);

  bolt_manager_label_device (mgr, dev);

  manager_maybe_auto_import_device (mgr, dev);

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
hanlde_udev_device_removed (BoltManager *mgr,
                            BoltDevice  *dev)
{
  const char *opath;
  const char *syspath;

  syspath = bolt_device_get_syspath (dev);
  bolt_msg (LOG_DEV (dev), "removed (%s)", syspath);

  manager_deregister_device (mgr, dev);

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

static void
handle_udev_device_attached (BoltManager        *mgr,
                             BoltDevice         *dev,
                             struct udev_device *udev)
{
  g_autoptr(BoltDevice) parent = NULL;
  BoltDomain *domain;
  const char *syspath;
  BoltStatus status;

  syspath = udev_device_get_syspath (udev);
  domain = manager_find_domain_by_syspath (mgr, syspath);

  if (domain == NULL)
    {
      bolt_warn (LOG_TOPIC ("domain"),
                 "could not find domain for device at '%s'",
                 syspath);
      return;
    }

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

  maybe_authorize_device (mgr, dev);
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
handle_store_device_removed (BoltStore   *store,
                             const char  *uid,
                             BoltManager *mgr)
{
  g_autoptr(BoltDevice) dev = NULL;
  BoltStatus status;
  const char *opath;

  dev = manager_find_device_by_uid (mgr, uid, NULL);
  bolt_msg (LOG_DEV (dev), "removed from store");

  if (!dev)
    return;

  /* TODO: maybe move to a new bolt_device_removed (dev) */
  g_object_set (dev,
                "store", NULL,
                "key", BOLT_KEY_MISSING,
                "policy", BOLT_POLICY_DEFAULT,
                NULL);

  status = bolt_device_get_status (dev);
  /* if the device is connected, keep it around */
  if (status != BOLT_STATUS_DISCONNECTED)
    return;

  manager_deregister_device (mgr, dev);
  opath = bolt_device_get_object_path (dev);

  if (opath == NULL)
    return;

  bolt_exported_emit_signal (BOLT_EXPORTED (mgr),
                             "DeviceRemoved",
                             g_variant_new ("(o)", opath),
                             NULL);

  bolt_device_unexport (dev);
  bolt_info (LOG_DEV (dev), "unexported");
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
      maybe_authorize_device (mgr, child);
    }
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
  timeout = mgr->probing_tsettle * MSEC_PER_USEC;
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

static gboolean
manager_maybe_power_controller (BoltManager *mgr)
{
  g_autoptr(GError) err = NULL;
  gboolean can_force_power;
  gboolean ok;
  int n;

  can_force_power = bolt_power_can_force (mgr->power);
  bolt_info (LOG_TOPIC ("power"), "force_power support: %s",
             bolt_yesno (can_force_power));

  if (can_force_power == FALSE)
    return FALSE;

  n = bolt_sysfs_count_domains (mgr->udev, &err);
  if (n < 0)
    {
      bolt_warn_err (err, LOG_TOPIC ("udev"),
                     "failed to count domains");
      return FALSE;
    }
  else if (n > 0)
    {
      ok = FALSE;
      goto out;
    }

  bolt_info (LOG_TOPIC ("power"), "setting force_power to ON");
  ok = bolt_power_force_switch (mgr->power, TRUE, &err);

  if (!ok)
    {
      bolt_warn_err (err, LOG_TOPIC ("power"),
                     "could not force power");
      return ok;
    }

  /* we wait for a total of 5.0 seconds, should hopefully
   * be enough for at least the domain to show up. */
  for (int i = 0; i < 25 && n < 1; i++)
    {
      g_usleep (200000); /* 200 000 us = 0.2s */
      n = bolt_sysfs_count_domains (mgr->udev, NULL);
    }

out:
  bolt_info (LOG_TOPIC ("udev"), "found %d domain%s",
             n, n > 1 ? "s" : "");
  return ok;
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
      g_propagate_error (error, g_steal_pointer (&err));
      return FALSE;
    }

  bolt_config_set_auth_mode (mgr->config, str);
  ok = bolt_store_config_save (mgr->store, mgr->config, &err);

  if (!ok)
    {
      bolt_warn_err (err, LOG_TOPIC ("config"), "error saving config");
      g_propagate_error (error, g_steal_pointer (&err));
      return FALSE;
    }

  mgr->authmode = authmode;
  bolt_info (LOG_TOPIC ("config"), "auth mode set to '%s'", str);
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
  BoltAuth *auth;
  BoltDevice *dev;
  BoltManager *mgr;
  GDBusMethodInvocation *inv;
  GError *error = NULL;
  const char *opath;
  gboolean ok;

  inv = user_data;
  dev = BOLT_DEVICE (device);
  auth = BOLT_AUTH (res);
  mgr = BOLT_MANAGER (bolt_auth_get_origin (auth));
  ok = bolt_auth_check (auth, &error);

  if (ok)
    {
      GVariant *params;
      const char *str;
      BoltPolicy policy;

      params = g_dbus_method_invocation_get_parameters (inv);
      g_variant_get_child (params, 1, "&s", &str);

      policy = bolt_enum_from_string (BOLT_TYPE_POLICY, str, NULL);
      if (policy == BOLT_POLICY_DEFAULT)
        policy = mgr->policy;

      ok = bolt_store_put_device (mgr->store,
                                  dev,
                                  policy,
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

  bolt_info (LOG_DEV (dev), "enrolling an authorized device");

  key = bolt_device_get_key_from_sysfs (dev, &err);
  if (key == NULL && !bolt_err_notfound (err))
    {
      bolt_warn_err (err, LOG_DEV (dev), LOG_TOPIC ("udev"),
                     "failed to read key from sysfs");
      g_propagate_prefixed_error (error, g_steal_pointer (&err), "%s",
                                  "could not determine existing authorization");
      return NULL;
    }

  ok = bolt_store_put_device (mgr->store, dev, policy, key, &err);

  if (!ok)
    {
      bolt_warn_err (err, LOG_DEV (dev), LOG_TOPIC ("store"),
                     "failed to store device");
      g_propagate_error (error, g_steal_pointer (&err));
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
  g_autoptr(BoltKey) key = NULL;
  BoltManager *mgr;
  const char *uid;
  BoltSecurity level;
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

  if (bolt_auth_mode_is_disabled (mgr->authmode))
    {
      g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED,
                   "authorization of new devices is disabled");
      return NULL;
    }

  if (bolt_device_supports_secure_mode (dev))
    level = bolt_device_get_security (dev);
  else
    level = BOLT_SECURITY_USER;

  key = NULL;
  if (level == BOLT_SECURITY_SECURE)
    key = bolt_key_new ();

  auth = bolt_auth_new (mgr, level, key);
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
  if (!bolt_exported_export (BOLT_EXPORTED (mgr),
                             connection,
                             BOLT_DBUS_PATH,
                             error))
    return FALSE;

  bolt_domain_foreach (mgr->domains,
                       (GFunc) bolt_domain_export,
                       connection);

  for (guint i = 0; i < mgr->devices->len; i++)
    {
      g_autoptr(GError) err  = NULL;
      BoltDevice *dev = g_ptr_array_index (mgr->devices, i);
      const char *opath;

      opath = bolt_device_export (dev, connection, &err);
      if (opath == NULL)
        {
          bolt_warn_err (err, LOG_DEV (dev), LOG_TOPIC ("dbus"),
                         "error exporting a device");
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

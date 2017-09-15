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


#include <gio/gio.h>
#include <glib.h>

#include <libudev.h>

#include <errno.h>
#include <stdio.h>

#include "device.h"
#include "ioutils.h"
#include "manager.h"
#include "store.h"

#include "enums.h"

G_DEFINE_QUARK (TB_ERROR, tb_error);

enum {
  TB_ERROR_FAILED = 0,
  TB_ERROR_UDEV,
};


G_DEFINE_AUTOPTR_CLEANUP_FUNC (GEnumClass, g_type_class_unref);

TbSecurity
tb_security_from_string (const char *str)
{
  g_autoptr(GEnumClass) klass = NULL;
  GEnumValue *value;

  if (str == NULL)
    return TB_SECURITY_UNKNOWN;

  klass = g_type_class_ref (TB_TYPE_SECURITY);
  value = g_enum_get_value_by_nick (klass, str);

  if (value == NULL)
    {
      g_warning ("Unknown security: %s", str);
      return TB_SECURITY_UNKNOWN;
    }

  return value->value;
}

char *
tb_security_to_string (TbSecurity security)
{
  g_autoptr(GEnumClass) klass = NULL;
  GEnumValue *value;

  klass = g_type_class_ref (TB_TYPE_SECURITY);
  value = g_enum_get_value (klass, security);

  return g_strdup (value->value_nick);
}


typedef struct udev_monitor udev_monitor;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (udev_monitor, udev_monitor_unref);

typedef struct udev_device udev_device;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (udev_device, udev_device_unref);

struct _TbManager
{
  GObject              object;

  struct udev         *udev;
  struct udev_monitor *udev_monitor;
  struct udev_monitor *kernel_monitor;
  GSource             *udev_source;
  GSource             *kernel_source;

  GPtrArray           *devices;

  /* assume for now we have only one domain */
  TbSecurity security;

  TbStore   *store;
};

enum {
  PROP_0,

  PROP_STORE,
  PROP_SECURITY,

  PROP_LAST
};

static GParamSpec *props[PROP_LAST] = {
  NULL,
};

enum {
  SIGNAL_DEVICE_ADDED,
  SIGNAL_DEVICE_REMOVED,
  SIGNAL_DEVICE_CHANGED,
  SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = {0};

static gboolean   tb_manager_initable_init (GInitable    *initable,
                                            GCancellable *cancellable,
                                            GError      **error);
static void       tb_manager_initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (TbManager,
                         tb_manager,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                tb_manager_initable_iface_init));

static void
tb_manager_finalize (GObject *object)
{
  TbManager *mgr = TB_MANAGER (object);

  if (mgr->udev_monitor)
    {
      udev_monitor_unref (mgr->udev_monitor);
      mgr->udev_monitor = NULL;

      g_source_destroy (mgr->udev_source);
      g_source_unref (mgr->udev_source);
    }

  if (mgr->kernel_monitor)
    {
      udev_monitor_unref (mgr->kernel_monitor);
      mgr->kernel_monitor = NULL;

      g_source_destroy (mgr->kernel_source);
      g_source_unref (mgr->kernel_source);
    }

  if (mgr->udev)
    {
      udev_unref (mgr->udev);
      mgr->udev = NULL;
    }

  if (mgr->store)
    g_clear_object (&mgr->store);

  g_ptr_array_free (mgr->devices, TRUE);

  G_OBJECT_CLASS (tb_manager_parent_class)->finalize (object);
}

static void
tb_manager_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  TbManager *mgr = TB_MANAGER (object);

  switch (prop_id)
    {
    case PROP_STORE:
      g_value_set_object (value, mgr->store);
      break;

    case PROP_SECURITY:
      g_value_set_enum (value, mgr->security);
      break;

    default:
      g_assert_not_reached ();
    }
}

static void
tb_manager_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{

  switch (prop_id)
    {
    case PROP_STORE:
      g_object_set_data (object, "db-path", g_value_dup_string (value));
      break;

    default:
      g_assert_not_reached ();
    }
}

static void
tb_manager_constructed (GObject *obj)
{
  TbManager *mgr        = TB_MANAGER (obj);

  g_autoptr(GFile) root = NULL;
  g_autofree char *path = NULL;

  path = g_object_steal_data (obj, "db-path");
  root = g_file_new_for_path (path);

  mgr->store = g_object_new (TB_TYPE_STORE, "root", root, NULL);
}

static void
tb_manager_init (TbManager *mgr)
{
  mgr->udev    = udev_new ();
  mgr->devices = g_ptr_array_new_with_free_func (g_object_unref);
}

static void
tb_manager_class_init (TbManagerClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = tb_manager_finalize;

  gobject_class->get_property = tb_manager_get_property;
  gobject_class->set_property = tb_manager_set_property;
  gobject_class->constructed  = tb_manager_constructed;

  /* properties */
  props[PROP_STORE] =
    g_param_spec_string ("db",
                         NULL, NULL,
                         "",
                         G_PARAM_READWRITE      |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_NAME);

  props[PROP_SECURITY] =
    g_param_spec_enum ("security",
                       NULL, NULL,
                       TB_TYPE_SECURITY,
                       TB_SECURITY_UNKNOWN,
                       G_PARAM_READABLE |
                       G_PARAM_STATIC_NAME);

  g_object_class_install_properties (gobject_class, PROP_LAST, props);

  /* signals */
  signals[SIGNAL_DEVICE_ADDED] =
    g_signal_new ("device-added",
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE,
                  1, G_TYPE_POINTER);

  signals[SIGNAL_DEVICE_REMOVED] =
    g_signal_new ("device-removed",
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE,
                  1, G_TYPE_POINTER);

  signals[SIGNAL_DEVICE_CHANGED] =
    g_signal_new ("device-changed",
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE,
                  1, G_TYPE_POINTER);
}

static void
tb_manager_initable_iface_init (GInitableIface *iface)
{
  iface->init = tb_manager_initable_init;
}

static guint
udev_device_get_sysfs_attr_uint (struct udev_device *device, const char *attr)
{
  const char *str;
  char *end;
  guint64 val;

  str = udev_device_get_sysattr_value (device, attr);

  if (str == NULL)
    return 0;

  val = g_ascii_strtoull (str, &end, 0);

  if (str == end)
    return 0;

  if (val > G_MAXUINT)
    {
      g_warning ("value read from sysfs overflows guint field.");
      val = 0;
    }

  return (guint) val;
}

static gint
udev_device_get_sysfs_attr_int (struct udev_device *device, const char *attr)
{
  const char *str;
  char *end;
  gint64 val;

  str = udev_device_get_sysattr_value (device, attr);

  if (str == NULL)
    return 0;

  val = g_ascii_strtoull (str, &end, 0);

  if (str == end)
    return 0;

  if (val > G_MAXINT)
    {
      g_warning ("value read from sysfs overflows guint field.");
      val = 0;
    }

  return (gint) val;
}

static void
device_update_from_udev (TbManager *mgr, TbDevice *dev, struct udev_device *device)
{
  TbAuthLevel old;
  int authorized;

  authorized = udev_device_get_sysfs_attr_int (device, "authorized");

  if (authorized < -1 || authorized > 2)
    authorized = TB_AUTH_LEVEL_UNKNOWN;

  old = tb_device_get_authorized (dev);

  if (old == authorized)
    return;

  g_object_set (dev, "authorized", authorized, NULL);
  g_signal_emit (mgr, signals[SIGNAL_DEVICE_CHANGED], 0, dev);
}

static TbDevice *
manager_devices_add_from_udev (TbManager *mgr, struct udev_device *device)
{
  g_autoptr(GError) err = NULL;
  TbDevice *dev;
  const char *uid;
  const char *device_name;
  const char *vendor_name;
  const char *sysfs;
  guint device_id;
  guint vendor_id;
  int authorized;
  gboolean ok;

  uid = udev_device_get_sysattr_value (device, "unique_id");
  if (uid == NULL)
    return NULL;

  sysfs       = udev_device_get_syspath (device);
  device_name = udev_device_get_sysattr_value (device, "device_name");
  device_id   = udev_device_get_sysfs_attr_uint (device, "device");
  vendor_name = udev_device_get_sysattr_value (device, "vendor_name");
  vendor_id   = udev_device_get_sysfs_attr_uint (device, "vendor");
  authorized  = udev_device_get_sysfs_attr_int (device, "authorized");

  if (authorized < -1 || authorized > 2)
    authorized = TB_AUTH_LEVEL_UNKNOWN;

  dev = g_object_new (TB_TYPE_DEVICE,
                      "uid", uid,
                      "device-name", device_name,
                      "device-id", device_id,
                      "vendor-name", vendor_name,
                      "vendor-id", vendor_id,
                      "sysfs", sysfs,
                      "authorized", authorized,
                      NULL);

  ok = tb_store_merge (mgr->store, dev, &err);

  if (!ok && !g_error_matches (err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
    g_warning ("Could not load device data from DB: %s", err->message);

  g_signal_emit (mgr, signals[SIGNAL_DEVICE_ADDED], 0, dev);

  g_ptr_array_add (mgr->devices, dev);
  return dev;
}

static TbDevice *
manager_devices_lookup_by_uid (TbManager *mgr, const char *uid)
{
  guint i;

  for (i = 0; i < mgr->devices->len; i++)
    {
      TbDevice *dev = g_ptr_array_index (mgr->devices, i);

      if (!g_strcmp0 (tb_device_get_uid (dev), uid))
        return dev;
    }

  return NULL;
}

static TbDevice *
manager_devices_lookup_by_udev (TbManager *mgr, struct udev_device *udev)
{
  const char *uid;
  guint i;

  uid = udev_device_get_sysattr_value (udev, "unique_id");

  if (uid != NULL)
    return manager_devices_lookup_by_uid (mgr, uid);

  for (i = 0; i < mgr->devices->len; i++)
    {
      TbDevice *dev = g_ptr_array_index (mgr->devices, i);
      const char *p_old = tb_device_get_sysfs_path (dev);
      const char *p_new;

      if (p_old == NULL)
        continue;

      p_new = udev_device_get_syspath (udev);

      if (!g_strcmp0 (p_old, p_new))
        return dev;
    }

  return NULL;
}

static gboolean
manager_uevent_kernel_cb (GIOChannel *source, GIOCondition condition, gpointer user_data)
{
  TbManager *mgr = TB_MANAGER (user_data);

  g_autoptr(udev_device) device = NULL;
  const char *action;

  device = udev_monitor_receive_device (mgr->kernel_monitor);

  if (device == NULL)
    return TRUE;

  action = udev_device_get_action (device);
  if (action == NULL)
    return TRUE;

  g_debug ("uevent [KERNEL]: %s", action);

  if (g_str_equal (action, "add"))
    manager_devices_add_from_udev (mgr, device);

  return TRUE;
}

static gboolean
manager_uevent_udev_cb (GIOChannel *source, GIOCondition condition, gpointer user_data)
{
  TbManager *mgr                = TB_MANAGER (user_data);

  g_autoptr(udev_device) device = NULL;
  const char *action;
  TbDevice *dev;

  device = udev_monitor_receive_device (mgr->udev_monitor);

  if (device == NULL)
    return TRUE;

  action = udev_device_get_action (device);
  if (action == NULL)
    return TRUE;

  g_debug ("uevent [ UDEV ]: %s", action);

  if (g_str_equal (action, "add") ||
      g_str_equal (action, "change"))
    {
      const char *uid = udev_device_get_sysattr_value (device, "unique_id");
      if (uid == NULL)
        return TRUE;

      dev = manager_devices_lookup_by_udev (mgr, device);
      if (dev == NULL && g_str_equal (action, "change"))
        {
          g_warning ("device not in list!");
          manager_devices_add_from_udev (mgr, device);
        }
      else
        {
          device_update_from_udev (mgr, dev, device);
        }

    }
  else if (g_strcmp0 (action, "remove") == 0)
    {
      dev = manager_devices_lookup_by_udev (mgr, device);
      if (!dev)
        return TRUE;

      g_signal_emit (mgr, signals[SIGNAL_DEVICE_REMOVED], 0, dev);
      g_object_set (dev, "authorized", TB_AUTH_LEVEL_UNKNOWN, "sysfs", NULL, NULL);
      g_ptr_array_remove_fast (mgr->devices, dev);
    }

  return TRUE;
}

static gboolean
setup_monitor (TbManager     *mgr,
               const char    *name,
               GSourceFunc    callback,
               udev_monitor **monitor_out,
               GSource      **watch_out,
               GError       **error)
{
  g_autoptr(udev_monitor) monitor;
  g_autoptr(GIOChannel) channel;
  GSource *watch;
  int fd;
  int res;

  monitor = udev_monitor_new_from_netlink (mgr->udev, name);
  if (monitor == NULL)
    {
      g_set_error_literal (error, TB_ERROR, TB_ERROR_UDEV,
                           "udev: could not create monitor");
      return FALSE;
    }

  udev_monitor_set_receive_buffer_size (monitor, 128 * 1024 * 1024);

  res = udev_monitor_filter_add_match_subsystem_devtype (monitor, "thunderbolt", NULL);
  if (res < 0)
    {
      g_set_error_literal (error, TB_ERROR, TB_ERROR_UDEV,
                           "udev: could not add match for 'thunderbolt' to monitor");
      return FALSE;
    }

  res = udev_monitor_enable_receiving (monitor);
  if (res < 0)
    {
      g_set_error_literal (error, TB_ERROR, TB_ERROR_UDEV,
                           "udev: could not enable monitoring");
      return FALSE;
    }

  fd = udev_monitor_get_fd (monitor);

  if (fd < 0)
    {
      g_set_error_literal (error, TB_ERROR, TB_ERROR_UDEV,
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
tb_manager_initable_init (GInitable *initable, GCancellable *cancellable, GError **error)
{
  TbManager *mgr = TB_MANAGER (initable);
  struct udev_enumerate *enumerate;
  struct udev_list_entry *l, *devices;
  gboolean ok;

  ok = setup_monitor (mgr, "kernel",
                      (GSourceFunc) manager_uevent_kernel_cb,
                      &mgr->kernel_monitor, &mgr->kernel_source,
                      error);

  if (!ok)
    return FALSE;

  ok = setup_monitor (mgr, "udev",
                      (GSourceFunc) manager_uevent_udev_cb,
                      &mgr->udev_monitor, &mgr->udev_source,
                      error);

  if (!ok)
    return FALSE;

  /* TODO: error checking */
  enumerate = udev_enumerate_new (mgr->udev);
  udev_enumerate_add_match_subsystem (enumerate, "thunderbolt");
  udev_enumerate_scan_devices (enumerate);
  devices = udev_enumerate_get_list_entry (enumerate);

  for (l = devices; l; l = udev_list_entry_get_next (l))
    {
      g_autoptr(udev_device) udevice = NULL;
      TbDevice *dev;

      udevice = udev_device_new_from_syspath (udev_enumerate_get_udev (enumerate),
                                              udev_list_entry_get_name (l));

      if (udevice == NULL)
        continue;

      dev = manager_devices_add_from_udev (mgr, udevice);

      if (dev == NULL)
        {
          const char *security;
          security = udev_device_get_sysattr_value (udevice, "security");

          if (security != NULL)
            mgr->security = tb_security_from_string (security);

          continue;
        }
    }

  return TRUE;
}

TbManager *
tb_manager_new (GError **error)
{
  TbManager *mgr;

  mgr = g_initable_new (TB_TYPE_MANAGER,
                        NULL, error,
                        "db", "/var/lib/tb",
                        NULL);

  return mgr;
}

const GPtrArray *
tb_manager_list_attached (TbManager *mgr)
{
  return mgr->devices;
}

TbDevice *
tb_manager_lookup (TbManager *mgr, const char *uid)
{
  TbDevice *dev;

  dev = manager_devices_lookup_by_uid (mgr, uid);

  if (dev == NULL)
    return NULL;

  return g_object_ref (dev);
}

gboolean
tb_manager_device_stored (TbManager *mgr, TbDevice *dev)
{
  const char *uid;

  g_return_val_if_fail (mgr != NULL, FALSE);
  g_return_val_if_fail (dev != NULL, FALSE);

  uid = tb_device_get_uid (dev);

  return tb_store_have (mgr->store, uid);
}

gboolean
tb_manager_store (TbManager *mgr, TbDevice *device, GError **error)
{
  return tb_store_put (mgr->store, device, error);
}

gboolean
tb_manager_have_key (TbManager *mgr, TbDevice *dev)
{
  const char *uid = tb_device_get_uid (dev);

  g_return_val_if_fail (mgr != NULL, FALSE);
  g_return_val_if_fail (uid != NULL, FALSE);

  return tb_store_have_key (mgr->store, uid);
}

int
tb_manager_ensure_key (TbManager *mgr,
                       TbDevice  *dev,
                       gboolean   replace,
                       gboolean  *created,
                       GError   **error)
{
  const char *uid;
  int fd;

  if (replace)
    {
      fd = tb_store_create_key (mgr->store, dev, error);
      *created = TRUE;
      if (fd < 0)
        return fd;
    }

  uid = tb_device_get_uid (dev);
  fd  = tb_store_open_key (mgr->store, uid, error);

  if (fd > -1)
    {
      *created = FALSE;
      return fd;
    }

  fd = tb_store_create_key (mgr->store, dev, error);
  *created = TRUE;

  return fd;
}

TbSecurity
tb_manager_get_security (TbManager *mgr)
{
  g_return_val_if_fail (mgr != NULL, -1);

  if (mgr->security == TB_SECURITY_UNKNOWN)
    g_critical ("security level could not be determined");

  return mgr->security;
}

static gboolean
copy_key (int from, int to, GError **error)
{
  char buffer[TB_KEY_CHARS] = { 0, };
  ssize_t n, k;

  /* NB: need to write the key in one go, no chuncked i/o */
  n = tb_read_all (from, buffer, sizeof (buffer), error);
  if (n < 0)
    {
      return FALSE;
    }
  else if (n != sizeof (buffer))
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                           "Could not read entire key from disk");
      return FALSE;
    }

  do
    k = write (to, buffer, (size_t) n);
  while (k < 0 && errno == EINTR);

  if (k != n)
    {
      g_set_error_literal (error, G_IO_ERROR,
                           g_io_error_from_errno (errno),
                           "io error while writing key data");
      return FALSE;
    }

  return TRUE;
}

gboolean
tb_manager_authorize (TbManager *mgr, TbDevice *dev, GError **error)
{
  g_autoptr(DIR) d = NULL;
  const char *sysfs;
  const char *uid;
  TbSecurity security;
  gboolean ok;
  int fd = -1;

  g_return_val_if_fail (dev != NULL, FALSE);

  uid = tb_device_get_uid (dev);

  security = tb_manager_get_security (mgr);

  if (security < TB_SECURITY_USER)
    /* nothing to do */
    return TRUE;

  sysfs = tb_device_get_sysfs_path (dev);
  g_assert (sysfs != NULL);

  d = tb_opendir (sysfs, error);
  if (d == NULL)
    return FALSE;

  /* openat is used here to be absolutely sure that the
   * directory that contains the right 'unique_id' is the
   * one we are authorizing */
  fd = tb_openat (d, "unique_id", O_RDONLY, error);
  if (fd < 0)
    return FALSE;

  ok = tb_verify_uid (fd, uid, error);
  if (!ok)
    {
      close (fd);
      return FALSE;
    }

  close (fd);

  if (security == TB_SECURITY_SECURE)
    {
      gboolean created = FALSE;
      int to = -1, from = -1;

      from = tb_manager_ensure_key (mgr, dev, FALSE, &created, error);
      if (from < 0)
        return FALSE;

      to = tb_openat (d, "key", O_WRONLY, error);
      if (to < 0)
        {
          close (from);
          return FALSE;
        }

      ok = copy_key (from, to, error);

      close (from);          /* ignore close's return on read */

      if (!ok)
        {
          close (to);
          return FALSE;
        }

      ok = tb_close (to, error);
      if (!ok)
        return FALSE;

      if (created)
        security = TB_SECURITY_USER;
    }

  fd = tb_openat (d, "authorized", O_WRONLY, error);

  if (fd < 0)
    return FALSE;

  ok = tb_write_char (fd, security, error);
  if (!ok)
    {
      close (fd);
      return FALSE;
    }

  return tb_close (fd, error);
}

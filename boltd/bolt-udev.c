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

#include "bolt-udev.h"

#include "bolt-error.h"
#include "bolt-sysfs.h"

#include <libudev.h>
#include <string.h>

#include <errno.h>

typedef struct udev_monitor udev_monitor;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (udev_monitor, udev_monitor_unref);

typedef struct udev_device udev_device;
G_DEFINE_AUTOPTR_CLEANUP_FUNC (udev_device, udev_device_unref);

static void     udev_initable_iface_init (GInitableIface *iface);

static gboolean bolt_udev_initialize (GInitable    *initable,
                                      GCancellable *cancellable,
                                      GError      **error);
/*  */
struct _BoltUdev
{
  GObject object;

  /* the native udev things */
  struct udev         *udev;
  struct udev_monitor *monitor;
  GSource             *source;

  /* properties */
  char *name;
  GStrv filter;
};

enum {
  PROP_0,
  PROP_NAME,
  PROP_FILTER,

  PROP_LAST
};

static GParamSpec *props[PROP_LAST] = { NULL, };

enum {
  SIGNAL_UEVENT,
  SIGNAL_LAST,
};

static guint signals[SIGNAL_LAST] = { 0, };


G_DEFINE_TYPE_WITH_CODE (BoltUdev,
                         bolt_udev,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                udev_initable_iface_init));

static void
bolt_udev_finalize (GObject *object)
{
  BoltUdev *udev = BOLT_UDEV (object);

  if (udev->monitor)
    {
      udev_monitor_unref (udev->monitor);
      udev->monitor = NULL;

      g_source_destroy (udev->source);
      g_source_unref (udev->source);
      udev->source = NULL;
    }

  g_clear_pointer (&udev->udev, udev_unref);

  g_clear_pointer (&udev->name, g_free);
  g_clear_pointer (&udev->filter, g_strfreev);

  G_OBJECT_CLASS (bolt_udev_parent_class)->finalize (object);
}


static void
bolt_udev_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
  BoltUdev *udev = BOLT_UDEV (object);

  switch (prop_id)
    {
    case PROP_NAME:
      g_value_set_string (value, udev->name);
      break;

    case PROP_FILTER:
      g_value_set_boxed (value, udev->filter);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bolt_udev_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
  BoltUdev *udev = BOLT_UDEV (object);

  switch (prop_id)
    {
    case PROP_NAME:
      udev->name = g_value_dup_string (value);
      break;

    case PROP_FILTER:
      udev->filter = g_value_dup_boxed (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bolt_udev_init (BoltUdev *udev)
{
}

static void
bolt_udev_class_init (BoltUdevClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = bolt_udev_get_property;
  gobject_class->set_property = bolt_udev_set_property;
  gobject_class->finalize     = bolt_udev_finalize;

  props[PROP_NAME] =
    g_param_spec_string ("name",
                         NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_FILTER] =
    g_param_spec_boxed ("filter",
                        NULL, NULL,
                        G_TYPE_STRV,
                        G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     props);

  signals[SIGNAL_UEVENT] =
    g_signal_new ("uevent",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL,
                  NULL,
                  g_cclosure_marshal_generic,
                  G_TYPE_NONE,
                  2,
                  G_TYPE_STRING,
                  G_TYPE_POINTER);
}

static void
udev_initable_iface_init (GInitableIface *iface)
{
  iface->init = bolt_udev_initialize;
}

/* internal methods */
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
setup_monitor (BoltUdev      *udev,
               const char    *name,
               const GStrv    filter,
               GSourceFunc    callback,
               udev_monitor **monitor_out,
               GSource      **watch_out,
               GError       **error)
{
  g_autoptr(udev_monitor) monitor = NULL;
  g_autoptr(GIOChannel) channel = NULL;
  GSource *watch;
  gboolean ok;
  int fd;
  int res;

  monitor = udev_monitor_new_from_netlink (udev->udev, name);
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

  g_source_set_callback (watch, callback, udev, NULL);
  g_source_attach (watch, g_main_context_get_thread_default ());

  *monitor_out = udev_monitor_ref (monitor);
  *watch_out   = watch;

  return TRUE;
}

static gboolean
handle_uevent_udev (GIOChannel  *source,
                    GIOCondition condition,
                    gpointer     user_data)
{
  g_autoptr(udev_device) device = NULL;
  BoltUdev *udev;
  const char *action;
  const char *syspath;

  udev = BOLT_UDEV (user_data);
  device = udev_monitor_receive_device (udev->monitor);

  if (device == NULL)
    return G_SOURCE_CONTINUE;

  action = udev_device_get_action (device);
  if (action == NULL)
    return G_SOURCE_CONTINUE;

  syspath = udev_device_get_syspath (device);
  if (syspath == NULL)
    return G_SOURCE_CONTINUE;

  g_signal_emit (udev, signals[SIGNAL_UEVENT], 0,
                 action, device);

  return G_SOURCE_CONTINUE;
}

static gboolean
bolt_udev_initialize (GInitable    *initable,
                      GCancellable *cancellable,
                      GError      **error)
{
  BoltUdev *udev = BOLT_UDEV (initable);
  gboolean ok;

  udev->udev = udev_new ();
  if (udev->udev == NULL)
    {
      g_set_error_literal (error, BOLT_ERROR, BOLT_ERROR_UDEV,
                           "udev: could not create udev handle");
      return FALSE;
    }

  ok = setup_monitor (udev, udev->name,
                      udev->filter,
                      (GSourceFunc) handle_uevent_udev,
                      &udev->monitor, &udev->source,
                      error);
  return ok;
}

/* public methods */
BoltUdev  *
bolt_udev_new (const char         *name,
               const char * const *filter,
               GError            **error)
{
  BoltUdev *udev;

  udev = g_initable_new (BOLT_TYPE_UDEV,
                         NULL, error,
                         "name", name,
                         "filter", filter,
                         NULL);

  return udev;
}


struct udev_enumerate *
bolt_udev_new_enumerate (BoltUdev *udev,
                         GError  **error)
{
  struct udev_enumerate *e;

  g_return_val_if_fail (BOLT_IS_UDEV (udev), NULL);

  e = udev_enumerate_new (udev->udev);

  if (e == NULL)
    g_set_error (error, BOLT_ERROR, BOLT_ERROR_UDEV,
                 "could not enumerate udev: %s",
                 g_strerror (errno));

  return e;
}

struct udev_device *
bolt_udev_device_new_from_syspath (BoltUdev   *udev,
                                   const char *syspath,
                                   GError    **error)
{
  struct udev_device *dev;

  g_return_val_if_fail (BOLT_IS_UDEV (udev), NULL);
  g_return_val_if_fail (syspath != NULL, NULL);

  dev = udev_device_new_from_syspath (udev->udev, syspath);

  if (dev == NULL)
    g_set_error (error, BOLT_ERROR, BOLT_ERROR_UDEV,
                 "could not create udev device: %s",
                 g_strerror (errno));

  return dev;
}

/* thunderbolt specific helpers */
int
bolt_udev_count_hosts (BoltUdev *udev,
                       GError  **error)
{
  g_return_val_if_fail (BOLT_IS_UDEV (udev), -1);

  return bolt_sysfs_count_hosts (udev->udev, error);
}

gboolean
bolt_udev_detect_force_power (BoltUdev *udev,
                              char    **result,
                              GError  **error)
{
  struct udev_enumerate *e;
  struct udev_list_entry *l, *devices;

  g_return_val_if_fail (BOLT_IS_UDEV (udev), FALSE);
  g_return_val_if_fail (result != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  e = bolt_udev_new_enumerate (udev, NULL);
  udev_enumerate_add_match_subsystem (e, "wmi");
  udev_enumerate_add_match_property (e, "DRIVER", "intel-wmi-thunderbolt");

  udev_enumerate_scan_devices (e);
  devices = udev_enumerate_get_list_entry (e);

  udev_list_entry_foreach (l, devices)
    {
      g_autofree char *path = NULL;
      const char *syspath;

      syspath = udev_list_entry_get_name (l);
      path = g_build_filename (syspath, "force_power", NULL);

      if (g_file_test (path, G_FILE_TEST_IS_REGULAR))
        {
          *result = g_steal_pointer (&path);
          break;
        }
    }

  udev_enumerate_unref (e);

  return TRUE;
}

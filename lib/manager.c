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
#include <gudev/gudev.h>

#include <stdio.h>

#include "device.h"
#include "manager.h"
#include "store.h"

struct _TbManagerClass
{
  GObjectClass parent_class;

  gpointer     padding[13];
};

struct _TbManager
{
  GObject      object;

  GUdevClient *udev;
  GPtrArray   *devices;

  /* assume for now we have only one domain */
  char    *security;

  TbStore *store;
};

enum { PROP_0,

       PROP_STORE,
       PROP_SECURITY,

       PROP_LAST };

static GParamSpec *props[PROP_LAST] = {
  NULL,
};

static gboolean tb_manager_initable_init (GInitable    *initable,
                                          GCancellable *cancellable,
                                          GError      **error);
static void tb_manager_initable_iface_init (GInitableIface *iface);

static void manager_uevent_cb (GUdevClient *client,
                               const gchar *action,
                               GUdevDevice *device,
                               gpointer     user_data);

G_DEFINE_TYPE_WITH_CODE (TbManager,
                         tb_manager,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE, tb_manager_initable_iface_init));

static void
tb_manager_finalize (GObject *object)
{
  TbManager *mgr = TB_MANAGER (object);

  if (mgr->udev)
    g_clear_object (&mgr->udev);

  if (mgr->store)
    g_clear_object (&mgr->store);

  g_ptr_array_free (mgr->devices, TRUE);

  G_OBJECT_CLASS (tb_manager_parent_class)->finalize (object);
}

static void
tb_manager_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  TbManager *mgr = TB_MANAGER (object);

  switch (prop_id)
    {
    case PROP_STORE:
      g_value_set_object (value, mgr->store);
      break;

    case PROP_SECURITY:
      g_value_set_string (value, mgr->security);
      break;
    }
}

static void
tb_manager_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{

  switch (prop_id)
    {
    case PROP_STORE:
      g_object_set_data (object, "db-path", g_value_dup_string (value));
      break;
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
  const char *subsystems[] = {"thunderbolt", NULL};

  mgr->udev    = g_udev_client_new (subsystems);
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

  props[PROP_STORE] =
    g_param_spec_string ("db", NULL, NULL, "", G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);

  props[PROP_SECURITY] = g_param_spec_string ("security", NULL, NULL, "", G_PARAM_READABLE | G_PARAM_STATIC_NAME);

  g_object_class_install_properties (gobject_class, PROP_LAST, props);
}

static void
tb_manager_initable_iface_init (GInitableIface *iface)
{
  iface->init = tb_manager_initable_init;
}

static TbDevice *
manager_devices_add_from_udev (TbManager *mgr, GUdevDevice *device)
{
  TbDevice *dev;
  const char *dev_name;

  g_autoptr(GError) err = NULL;
  gboolean ok;

  dev_name = g_udev_device_get_sysfs_attr (device, "device_name");
  if (dev_name == NULL)
    return NULL;

  dev = g_object_new (TB_TYPE_DEVICE, NULL);
  tb_device_update_from_udev (dev, device);

  ok = tb_store_merge (mgr->store, dev, &err);

  if (!ok && !g_error_matches (err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
    g_warning ("Could not load device data from DB: %s", err->message);

  g_ptr_array_add (mgr->devices, dev);
  return dev;
}

static void
tb_manager_devices_dump (TbManager *mgr)
{
  guint i;

  for (i = 0; i < mgr->devices->len; i++)
    {
      TbDevice *dev = g_ptr_array_index (mgr->devices, i);

      g_print ("%s\n", tb_device_get_name (dev));
      g_print (" uuid: %s\n", tb_device_get_uid (dev));
      g_print (" authorized: %d\n", tb_device_get_authorized (dev));
    }
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
manager_devices_lookup_by_udev (TbManager *mgr, GUdevDevice *udev)
{
  const char *uid;
  guint i;

  uid = g_udev_device_get_sysfs_attr (udev, "unique_id");

  if (uid != NULL)
    return manager_devices_lookup_by_uid (mgr, uid);

  for (i = 0; i < mgr->devices->len; i++)
    {
      TbDevice *dev     = g_ptr_array_index (mgr->devices, i);
      const char *p_old = tb_device_get_sysfs_path (dev);
      const char *p_new;

      if (p_old == NULL)
        continue;

      p_new = g_udev_device_get_sysfs_path (udev);

      if (!g_strcmp0 (p_old, p_new))
        return dev;
    }

  return NULL;
}

static void
manager_uevent_cb (GUdevClient *client, const gchar *action, GUdevDevice *device, gpointer user_data)
{
  TbManager *mgr = TB_MANAGER (user_data);
  TbDevice *dev;

  g_print ("uevent [%s]\n\n", action);

  if (g_strcmp0 (action, "add") == 0)
    {

      manager_devices_add_from_udev (mgr, device);

    }
  else if (g_strcmp0 (action, "change") == 0)
    {

      dev = manager_devices_lookup_by_udev (mgr, device);
      if (dev == NULL)
        {
          g_warning ("device not in list!\n");
          manager_devices_add_from_udev (mgr, device);
          return;
        }

      tb_device_update_from_udev (dev, device);

    }
  else if (g_strcmp0 (action, "remove") == 0)
    {
      dev = manager_devices_lookup_by_udev (mgr, device);

      if (dev)
        g_ptr_array_remove_fast (mgr->devices, dev);
      else
        g_warning ("device not in list!\n");
    }

  tb_manager_devices_dump (mgr);
}

static gboolean
tb_manager_initable_init (GInitable *initable, GCancellable *cancellable, GError **error)
{
  TbManager *mgr           = TB_MANAGER (initable);
  const char *subsystems[] = {"thunderbolt", NULL};
  GList *devices, *l;

  mgr->udev = g_udev_client_new (subsystems);

  g_signal_connect (mgr->udev, "uevent", G_CALLBACK (manager_uevent_cb), mgr);

  devices = g_udev_client_query_by_subsystem (mgr->udev, "thunderbolt");

  for (l = devices; l; l = l->next)
    {
      GUdevDevice *device = l->data;
      TbDevice *dev       = manager_devices_add_from_udev (mgr, device);

      if (dev == NULL)
        {
          const char *security = g_udev_device_get_sysfs_attr (device, "security");
          if (security != NULL)
            mgr->security = g_strdup (security);

          continue;
        }
    }

  //    tb_manager_devices_dump(mgr);

  return TRUE;
}

TbManager *
tb_manager_new (GError **error)
{
  TbManager *mgr;

  mgr = g_initable_new (TB_TYPE_MANAGER, NULL, error, "db", "/var/lib/tb", NULL);

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
tb_manager_store (TbManager *mgr, TbDevice *device, GError **error)
{
  return tb_store_put (mgr->store, device, error);
}

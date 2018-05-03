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

#include "bolt-store.h"

#include "bolt-error.h"
#include "bolt-fs.h"
#include "bolt-io.h"
#include "bolt-log.h"
#include "bolt-str.h"
#include "bolt-time.h"

#include <string.h>

/* ************************************  */
/* BoltStore */

struct _BoltStore
{
  GObject object;

  GFile  *root;
  GFile  *devices;
  GFile  *keys;
};


enum {
  PROP_STORE_0,

  PROP_ROOT,

  PROP_STORE_LAST
};

static GParamSpec *store_props[PROP_STORE_LAST] = { NULL, };


enum {
  SIGNAL_DEVICE_ADDED,
  SIGNAL_DEVICE_REMOVED,
  SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = {0};


G_DEFINE_TYPE (BoltStore,
               bolt_store,
               G_TYPE_OBJECT)


static void
bolt_store_finalize (GObject *object)
{
  BoltStore *store = BOLT_STORE (object);

  if (store->root)
    g_clear_object (&store->root);

  if (store->devices)
    g_clear_object (&store->devices);

  if (store->keys)
    g_clear_object (&store->keys);

  G_OBJECT_CLASS (bolt_store_parent_class)->finalize (object);
}

static void
bolt_store_init (BoltStore *store)
{
}

static void
bolt_store_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  BoltStore *store = BOLT_STORE (object);

  switch (prop_id)
    {
    case PROP_ROOT:
      g_value_set_object (value, store->root);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bolt_store_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  BoltStore *store = BOLT_STORE (object);

  switch (prop_id)
    {
    case PROP_ROOT:
      store->root = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bolt_store_constructed (GObject *obj)
{
  BoltStore *store = BOLT_STORE (obj);

  store->devices = g_file_get_child (store->root, "devices");
  store->keys = g_file_get_child (store->root, "keys");
}

static void
bolt_store_class_init (BoltStoreClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = bolt_store_finalize;

  gobject_class->constructed  = bolt_store_constructed;
  gobject_class->get_property = bolt_store_get_property;
  gobject_class->set_property = bolt_store_set_property;

  store_props[PROP_ROOT] =
    g_param_spec_object ("root",
                         NULL, NULL,
                         G_TYPE_FILE,
                         G_PARAM_READWRITE      |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_NAME);

  g_object_class_install_properties (gobject_class,
                                     PROP_STORE_LAST,
                                     store_props);

  signals[SIGNAL_DEVICE_ADDED] =
    g_signal_new ("device-added",
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE,
                  1, G_TYPE_STRING);

  signals[SIGNAL_DEVICE_REMOVED] =
    g_signal_new ("device-removed",
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE,
                  1, G_TYPE_STRING);
}

/* internal methods */

#define DEVICE_GROUP "device"
#define USER_GROUP "user"

#define CFG_FILE "boltd.conf"

/* public methods */

BoltStore *
bolt_store_new (const char *path)
{
  g_autoptr(GFile) root = NULL;
  BoltStore *store;

  root = g_file_new_for_path (path);
  store = g_object_new (BOLT_TYPE_STORE,
                        "root", root,
                        NULL);

  return store;
}

GKeyFile *
bolt_store_config_load (BoltStore *store,
                        GError   **error)
{
  g_autoptr(GKeyFile) kf = NULL;
  g_autoptr(GFile) sf = NULL;
  g_autofree char *data  = NULL;
  gboolean ok;
  gsize len;

  g_return_val_if_fail (store != NULL, FALSE);

  sf = g_file_get_child (store->root, CFG_FILE);
  ok = g_file_load_contents (sf, NULL,
                             &data, &len,
                             NULL,
                             error);

  if (!ok)
    return NULL;

  kf = g_key_file_new ();
  ok = g_key_file_load_from_data (kf, data, len, G_KEY_FILE_NONE, error);

  if (!ok)
    return NULL;

  return g_steal_pointer (&kf);
}

gboolean
bolt_store_config_save (BoltStore *store,
                        GKeyFile  *config,
                        GError   **error)
{
  g_autoptr(GFile) sf = NULL;
  g_autofree char *data  = NULL;
  gboolean ok;
  gsize len;

  sf = g_file_get_child (store->root, CFG_FILE);
  data = g_key_file_to_data (config, &len, error);

  if (!data)
    return FALSE;

  ok = g_file_replace_contents (sf,
                                data, len,
                                NULL, FALSE,
                                0,
                                NULL,
                                NULL, error);

  return ok;
}

GStrv
bolt_store_list_uids (BoltStore *store,
                      GError   **error)
{
  g_autoptr(GError) err = NULL;
  g_autoptr(GDir) dir   = NULL;
  g_autofree char *path = NULL;
  g_autoptr(GPtrArray) ids = NULL;
  const char *name;

  ids = g_ptr_array_new ();

  path = g_file_get_path (store->devices);
  dir = g_dir_open (path, 0, &err);
  if (dir == NULL)
    {
      if (bolt_err_notfound (err))
        return bolt_strv_from_ptr_array (&ids);

      g_propagate_error (error, g_steal_pointer (&err));
      return NULL;
    }

  while ((name = g_dir_read_name (dir)) != NULL)
    {
      if (g_str_has_prefix (name, "."))
        continue;

      g_ptr_array_add (ids, g_strdup (name));
    }

  return bolt_strv_from_ptr_array (&ids);
}

gboolean
bolt_store_put_device (BoltStore  *store,
                       BoltDevice *device,
                       BoltPolicy  policy,
                       BoltKey    *key,
                       GError    **error)
{
  g_autoptr(GFile) entry = NULL;
  g_autoptr(GKeyFile) kf = NULL;
  g_autofree char *data  = NULL;
  BoltDeviceType type;
  const char *uid;
  const char *label;
  gboolean ok;
  gint64 stime;
  gsize len;
  guint keystate = 0;

  g_return_val_if_fail (store != NULL, FALSE);
  g_return_val_if_fail (device != NULL, FALSE);

  uid = bolt_device_get_uid (device);
  g_assert (uid);

  entry = g_file_get_child (store->devices, uid);

  ok = bolt_fs_make_parent_dirs (entry, error);
  if (!ok)
    return FALSE;

  kf = g_key_file_new ();

  g_key_file_set_string (kf, DEVICE_GROUP, "name", bolt_device_get_name (device));
  g_key_file_set_string (kf, DEVICE_GROUP, "vendor", bolt_device_get_vendor (device));

  type = bolt_device_get_device_type (device);
  g_key_file_set_string (kf, DEVICE_GROUP, "type", bolt_device_type_to_string (type));

  if (policy != BOLT_POLICY_DEFAULT)
    {
      const char *str = bolt_policy_to_string (policy);
      g_key_file_set_string (kf, USER_GROUP, "policy", str);
    }

  label = bolt_device_get_label (device);

  if (label != NULL)
    g_key_file_set_string (kf, USER_GROUP, "label", label);

  stime = bolt_device_get_storetime (device);

  if (stime < 1)
    stime = (gint64) bolt_now_in_seconds ();

  g_key_file_set_uint64 (kf, USER_GROUP, "storetime", stime);

  data = g_key_file_to_data (kf, &len, error);

  if (!data)
    return FALSE;

  if (key)
    {
      g_autoptr(GError) err  = NULL;
      g_autoptr(GFile) keypath = g_file_get_child (store->keys, uid);
      ok = bolt_fs_make_parent_dirs (keypath, &err);

      if (ok)
        ok = bolt_key_save_file (key, keypath, &err);

      if (!ok)
        bolt_warn_err (err, "failed to store key");
      else
        keystate = bolt_key_get_state (key);
    }

  ok = g_file_replace_contents (entry,
                                data, len,
                                NULL, FALSE,
                                0,
                                NULL,
                                NULL, error);

  if (ok)
    {
      g_object_set (device,
                    "store", store,
                    "policy", policy,
                    "key", keystate,
                    "storetime", stime,
                    NULL);

      g_signal_emit (store, signals[SIGNAL_DEVICE_ADDED], 0, uid);
    }

  return ok;
}

BoltDevice *
bolt_store_get_device (BoltStore *store, const char *uid, GError **error)
{
  g_autoptr(GKeyFile) kf = NULL;
  g_autoptr(GFile) db = NULL;
  g_autoptr(GError) err = NULL;
  g_autofree char *name = NULL;
  g_autofree char *vendor = NULL;
  g_autofree char *data  = NULL;
  g_autofree char *typestr = NULL;
  g_autofree char *polstr = NULL;
  g_autofree char *label = NULL;
  BoltDeviceType type;
  BoltPolicy policy;
  BoltKeyState key;
  gboolean ok;
  guint64 stime;
  gsize len;

  g_return_val_if_fail (store != NULL, FALSE);
  g_return_val_if_fail (uid != NULL, FALSE);

  db = g_file_get_child (store->devices, uid);
  ok = g_file_load_contents (db, NULL,
                             &data, &len,
                             NULL,
                             error);

  if (!ok)
    return NULL;

  kf = g_key_file_new ();
  ok = g_key_file_load_from_data (kf, data, len, G_KEY_FILE_NONE, error);

  if (!ok)
    return NULL;

  name = g_key_file_get_string (kf, DEVICE_GROUP, "name", NULL);
  vendor = g_key_file_get_string (kf, DEVICE_GROUP, "vendor", NULL);
  typestr = g_key_file_get_string (kf, DEVICE_GROUP, "type", NULL);
  polstr = g_key_file_get_string (kf, USER_GROUP, "policy", NULL);
  label = g_key_file_get_string (kf, USER_GROUP, "label", NULL);

  type = bolt_enum_from_string (BOLT_TYPE_DEVICE_TYPE, typestr, &err);
  if (type == BOLT_DEVICE_UNKNOWN_TYPE)
    {
      bolt_warn_err (err, LOG_TOPIC ("store"), LOG_DEV_UID (uid),
                     "invalid device type");
      g_clear_error (&err);
      type = BOLT_DEVICE_PERIPHERAL;
    }

  policy = bolt_enum_from_string (BOLT_TYPE_POLICY, polstr, &err);
  if (policy == BOLT_POLICY_UNKNOWN)
    {
      bolt_warn_err (err, LOG_TOPIC ("store"), LOG_DEV_UID (uid),
                     "invalid policy");
      g_clear_error (&err);
      policy = BOLT_POLICY_MANUAL;
    }

  if (label != NULL)
    {
      g_autofree char *tmp = g_steal_pointer (&label);
      label = bolt_strdup_validate (tmp);
      if (label == NULL)
        bolt_warn (LOG_TOPIC ("store"), LOG_DEV_UID (uid),
                   "invalid device label: %s", label);
    }

  stime = g_key_file_get_uint64 (kf, USER_GROUP, "storetime", &err);
  if (err != NULL && !bolt_err_notfound (err))
    bolt_warn_err (err, LOG_TOPIC ("store"), "invalid enroll-time");

  if (stime == 0)
    {
      g_autoptr(GFileInfo) info = NULL;

      info = g_file_query_info (db,
                                "time::changed",
                                G_FILE_QUERY_INFO_NONE,
                                NULL, NULL);
      if (info != NULL)
        stime = g_file_info_get_attribute_uint64 (info, "time::changed");
    }


  key = bolt_store_have_key (store, uid);

  if (name == NULL || vendor == NULL)
    {
      g_set_error_literal (error, BOLT_ERROR, BOLT_ERROR_FAILED,
                           "invalid device entry in store");
      return NULL;
    }

  return g_object_new (BOLT_TYPE_DEVICE,
                       "uid", uid,
                       "name", name,
                       "vendor", vendor,
                       "type", type,
                       "status", BOLT_STATUS_DISCONNECTED,
                       "store", store,
                       "policy", policy,
                       "key", key,
                       "storetime", stime,
                       "label", label,
                       NULL);
}

gboolean
bolt_store_del_device (BoltStore  *store,
                       const char *uid,
                       GError    **error)
{
  g_autoptr(GFile) devpath = NULL;
  gboolean ok;

  devpath = g_file_get_child (store->devices, uid);
  ok = g_file_delete (devpath, NULL, error);

  if (ok)
    g_signal_emit (store, signals[SIGNAL_DEVICE_REMOVED], 0, uid);

  return ok;
}

BoltKeyState
bolt_store_have_key (BoltStore  *store,
                     const char *uid)
{
  g_autoptr(GFileInfo) keyinfo = NULL;
  g_autoptr(GFile) keypath = NULL;
  g_autoptr(GError) err = NULL;
  guint key = BOLT_KEY_MISSING;

  keypath = g_file_get_child (store->keys, uid);
  keyinfo = g_file_query_info (keypath, "standard::*", 0, NULL, &err);

  if (keyinfo != NULL)
    key = BOLT_KEY_HAVE; /* todo: check size */
  else if (!bolt_err_notfound (err))
    bolt_warn_err (err, LOG_DEV_UID (uid), "error querying key info");

  return key;
}

BoltKey *
bolt_store_get_key (BoltStore  *store,
                    const char *uid,
                    GError    **error)
{
  g_autoptr(GFile) keypath = NULL;

  keypath = g_file_get_child (store->keys, uid);

  return bolt_key_load_file (keypath, error);
}

gboolean
bolt_store_del_key (BoltStore  *store,
                    const char *uid,
                    GError    **error)
{
  g_autoptr(GFile) keypath = NULL;
  gboolean ok;

  keypath = g_file_get_child (store->keys, uid);
  ok = g_file_delete (keypath, NULL, error);

  return ok;
}

gboolean
bolt_store_del (BoltStore  *store,
                BoltDevice *dev,
                GError    **error)
{
  g_autoptr(GError) err = NULL;
  const char *uid;
  gboolean ok;

  g_return_val_if_fail (store != NULL, FALSE);
  g_return_val_if_fail (dev != NULL, FALSE);

  uid = bolt_device_get_uid (dev);

  ok = bolt_store_del_key (store, uid, &err);
  if (!ok && !bolt_err_notfound (err))
    {
      g_propagate_prefixed_error (error,
                                  g_steal_pointer (&err),
                                  "could not delete key: ");
      return FALSE;
    }

  ok = bolt_store_del_device (store, uid, error);

  if (ok)
    {
      g_object_set (dev,
                    "store", NULL,
                    "key", BOLT_KEY_MISSING,
                    "policy", BOLT_POLICY_DEFAULT,
                    NULL);
    }

  return ok;
}

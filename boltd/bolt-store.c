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

static void     bolt_store_initable_iface_init (GInitableIface *iface);


static gboolean bolt_store_initialize (GInitable    *initable,
                                       GCancellable *cancellable,
                                       GError      **error);

static gboolean bolt_store_init_store (DIR     *root,
                                       GError **error);

struct _BoltStore
{
  GObject object;

  GFile  *root;
  GFile  *domains;
  GFile  *devices;
  GFile  *keys;
  GFile  *times;

  guint   version;
};


enum {
  PROP_STORE_0,

  PROP_ROOT,
  PROP_VERSION,

  PROP_STORE_LAST
};

static GParamSpec *store_props[PROP_STORE_LAST] = { NULL, };


enum {
  SIGNAL_DEVICE_ADDED,
  SIGNAL_DEVICE_REMOVED,
  SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = {0};


G_DEFINE_TYPE_WITH_CODE (BoltStore,
                         bolt_store,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                bolt_store_initable_iface_init));


static void
bolt_store_finalize (GObject *object)
{
  BoltStore *store = BOLT_STORE (object);

  g_clear_object (&store->root);
  g_clear_object (&store->domains);
  g_clear_object (&store->devices);
  g_clear_object (&store->keys);
  g_clear_object (&store->times);

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

    case PROP_VERSION:
      g_value_set_uint (value, store->version);
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
  g_autofree char *path = NULL;
  BoltStore *store = BOLT_STORE (obj);

  G_OBJECT_CLASS (bolt_store_parent_class)->constructed (obj);

  path = g_file_get_path (store->root);

  bolt_info (LOG_TOPIC ("store"), "located at: %s", path);

  store->devices = g_file_get_child (store->root, "devices");
  store->domains = g_file_get_child (store->root, "domains");
  store->keys = g_file_get_child (store->root, "keys");
  store->times = g_file_get_child (store->root, "times");
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

  store_props[PROP_VERSION] =
    g_param_spec_uint ("version",
                       NULL, NULL,
                       0, G_MAXUINT, 0,
                       G_PARAM_READABLE |
                       G_PARAM_STATIC_STRINGS);

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

static void
bolt_store_initable_iface_init (GInitableIface *iface)
{
  iface->init = bolt_store_initialize;
}

static gboolean
bolt_store_initialize (GInitable    *initable,
                       GCancellable *cancellable,
                       GError      **error)
{
  g_autoptr(GError) err = NULL;
  g_autoptr(DIR) root = NULL;
  g_autofree char *path = NULL;
  BoltStore *store = BOLT_STORE (initable);
  gboolean ok;

  path = g_file_get_path (store->root);
  root = bolt_opendir (path, error);

  if (root == NULL)
    return FALSE;

  ok = bolt_store_init_store (root, error);
  if (!ok)
    return FALSE;

  ok = bolt_read_uint_at (dirfd (root),
                          "version",
                          &store->version,
                          &err);

  if (!ok && !bolt_err_notfound (err))
    return bolt_error_propagate (error, &err);

  return TRUE;
}

/* internal methods */
#define DOMAIN_GROUP "domain"
#define DEVICE_GROUP "device"
#define USER_GROUP "user"

#define CFG_FILE "boltd.conf"

static gboolean
bolt_store_init_store (DIR     *root,
                       GError **error)
{
  gboolean empty;
  gboolean ok;

  /* initialize an empty store with the basic layout,
   * which currently is just a 'version' field, since
   * all other directories are created on-demand */

  ok = bolt_dir_is_empty (root, &empty, error);
  if (!ok)
    return FALSE;

  bolt_debug (LOG_TOPIC ("store"), "needs init: %s",
              bolt_yesno (empty));

  if (!empty)
    return TRUE;

  bolt_info (LOG_TOPIC ("store"), "initializing");

  /* store is empty, create the 'version' file */
  ok = bolt_write_uint_at (dirfd (root),
                           "version",
                           BOLT_STORE_VERSION,
                           error);
  return ok;
}

/* public methods */

BoltStore *
bolt_store_new (const char *path, GError **error)
{
  g_autoptr(GFile) root = NULL;
  BoltStore *store;

  root = g_file_new_for_path (path);
  store = g_initable_new (BOLT_TYPE_STORE,
                          NULL, error,
                          "root", root,
                          NULL);
  return store;
}

guint
bolt_store_get_version (BoltStore *store)
{
  g_return_val_if_fail (BOLT_IS_STORE (store), 0);

  return store->version;
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

  g_return_val_if_fail (store != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

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

  g_return_val_if_fail (store != NULL, FALSE);
  g_return_val_if_fail (config != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

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
bolt_store_list_uids (BoltStore  *store,
                      const char *type,
                      GError    **error)
{
  g_autoptr(GError) err = NULL;
  g_autoptr(GDir) dir   = NULL;
  g_autoptr(GPtrArray) ids = NULL;
  g_autofree char *path = NULL;
  const char *name;

  g_return_val_if_fail (BOLT_IS_STORE (store), NULL);
  g_return_val_if_fail (type != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  if (bolt_streq (type, "devices"))
    path = g_file_get_path (store->devices);
  if (bolt_streq (type, "domains"))
    path = g_file_get_path (store->domains);

  if (path == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "unknown stored typed '%s'", type);
      return NULL;
    }

  ids = g_ptr_array_new ();

  dir = g_dir_open (path, 0, &err);
  if (dir == NULL)
    {
      if (bolt_err_notfound (err))
        return bolt_strv_from_ptr_array (&ids);

      bolt_error_propagate (error, &err);
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
bolt_store_put_domain (BoltStore  *store,
                       BoltDomain *domain,
                       GError    **error)
{
  g_autoptr(GError) err = NULL;
  g_autoptr(GFile) entry = NULL;
  g_autoptr(GKeyFile) kf = NULL;
  g_autofree char *path = NULL;
  char * const * bootacl = NULL;
  const char *uid;
  gboolean ok;
  gsize len;

  g_return_val_if_fail (BOLT_IS_STORE (store), FALSE);
  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  uid = bolt_domain_get_uid (domain);
  g_assert (uid);

  entry = g_file_get_child (store->domains, uid);

  ok = bolt_fs_make_parent_dirs (entry, error);
  if (!ok)
    return FALSE;

  kf = g_key_file_new ();
  path = g_file_get_path (entry);
  ok = g_key_file_load_from_file (kf,
                                  path,
                                  G_KEY_FILE_KEEP_COMMENTS,
                                  &err);

  if (!ok && !bolt_err_notfound (err))
    {
      bolt_warn_err (err, LOG_TOPIC ("store"),
                     "error loading existing domain");
      g_clear_error (&err);
      /* not fatal, keep going */
    }

  bootacl = bolt_domain_get_bootacl (domain);
  len = bolt_strv_length (bootacl);

  g_key_file_set_string_list (kf,
                              DOMAIN_GROUP,
                              "bootacl",
                              (const char * const *) bootacl,
                              len);

  ok = g_key_file_save_to_file (kf, path, error);

  if (!ok)
    return FALSE;

  g_object_set (G_OBJECT (domain),
                "store", store,
                NULL);

  return ok;
}

BoltDomain *
bolt_store_get_domain (BoltStore  *store,
                       const char *uid,
                       GError    **error)
{
  g_autoptr(GKeyFile) kf = NULL;
  g_autoptr(GFile) db = NULL;
  g_autoptr(GError) err = NULL;
  g_auto(GStrv) bootacl = NULL;
  g_autofree char *path = NULL;
  BoltDomain *domain = NULL;
  gboolean ok;

  g_return_val_if_fail (store != NULL, NULL);
  g_return_val_if_fail (uid != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  db = g_file_get_child (store->domains, uid);
  path = g_file_get_path (db);

  kf = g_key_file_new ();
  ok = g_key_file_load_from_file (kf, path, G_KEY_FILE_NONE, error);

  if (!ok)
    return NULL;

  bootacl = g_key_file_get_string_list (kf,
                                        DOMAIN_GROUP,
                                        "bootacl",
                                        NULL,
                                        &err);

  if (bootacl == NULL && !bolt_err_notfound (err))
    {
      bolt_warn_err (err, LOG_DOM_UID (uid), LOG_TOPIC ("store"),
                     "failed to parse bootacl for domain '%s'",
                     uid);

      g_clear_error (&err);
    }

  if (bolt_strv_isempty (bootacl))
    g_clear_pointer (&bootacl, g_strfreev);

  domain = g_object_new (BOLT_TYPE_DOMAIN,
                         "store", store,
                         "uid", uid,
                         "bootacl", bootacl,
                         NULL);

  return domain;
}

gboolean
bolt_store_del_domain (BoltStore  *store,
                       BoltDomain *domain,
                       GError    **error)
{
  g_autoptr(GFile) path = NULL;
  const char *uid;
  gboolean ok;

  g_return_val_if_fail (store != NULL, FALSE);
  g_return_val_if_fail (domain != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  ok = bolt_domain_can_delete (domain, error);
  if (!ok)
    return FALSE;

  uid = bolt_domain_get_uid (domain);

  path = g_file_get_child (store->domains, uid);
  ok = g_file_delete (path, NULL, error);

  if (!ok)
    return FALSE;

  g_object_set (domain,
                "store", NULL,
                NULL);

  return TRUE;
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
  g_autoptr(GError) err = NULL;
  g_autofree char *data = NULL;
  g_autofree char *path = NULL;
  BoltDeviceType type;
  const char *uid;
  const char *label;
  gboolean fresh;
  gboolean ok;
  guint64 ctime;
  guint64 atime;
  guint64 stime;
  guint64 gen;
  gsize len;
  guint keystate;

  g_return_val_if_fail (BOLT_IS_STORE (store), FALSE);
  g_return_val_if_fail (BOLT_IS_DEVICE (device), FALSE);
  g_return_val_if_fail (key == NULL || BOLT_IS_KEY (key), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  uid = bolt_device_get_uid (device);
  g_assert (uid);

  entry = g_file_get_child (store->devices, uid);

  ok = bolt_fs_make_parent_dirs (entry, error);
  if (!ok)
    return FALSE;

  kf = g_key_file_new ();

  path = g_file_get_path (entry);

  ok = g_key_file_load_from_file (kf,
                                  path,
                                  G_KEY_FILE_KEEP_COMMENTS,
                                  &err);

  if (!ok)
    {
      if (!bolt_err_notfound (err))
        bolt_warn_err (err, LOG_TOPIC ("store"), LOG_DEV_UID (uid),
                       "could not load previously stored device");
      g_clear_error (&err);
    }

  g_key_file_set_string (kf, DEVICE_GROUP, "name", bolt_device_get_name (device));
  g_key_file_set_string (kf, DEVICE_GROUP, "vendor", bolt_device_get_vendor (device));

  gen = bolt_device_get_generation (device);
  if (gen)
    g_key_file_set_uint64 (kf, DEVICE_GROUP, "generation", gen);

  type = bolt_device_get_device_type (device);
  g_key_file_set_string (kf, DEVICE_GROUP, "type", bolt_device_type_to_string (type));

  if (policy == BOLT_POLICY_DEFAULT)
    policy = bolt_device_get_policy (device);

  if (policy != BOLT_POLICY_DEFAULT)
    {
      const char *str = bolt_policy_to_string (policy);
      g_key_file_set_string (kf, USER_GROUP, "policy", str);
    }

  label = bolt_device_get_label (device);

  if (label != NULL)
    g_key_file_set_string (kf, USER_GROUP, "label", label);

  stime = bolt_device_get_storetime (device);

  if (stime == 0)
    stime = bolt_now_in_seconds ();

  g_key_file_set_uint64 (kf, USER_GROUP, "storetime", stime);

  data = g_key_file_to_data (kf, &len, error);

  if (!data)
    return FALSE;

  keystate = bolt_device_get_keystate (device);
  if (key)
    {
      ok = bolt_store_put_key (store, uid, key, &err);

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

  if (!ok)
    return FALSE;

  fresh = bolt_device_get_stored (device) == FALSE;

  g_object_set (device,
                "store", store,
                "policy", policy,
                "key", keystate,
                "storetime", stime,
                NULL);

  if (fresh)
    g_signal_emit (store, signals[SIGNAL_DEVICE_ADDED], 0, uid);

  ctime = bolt_device_get_conntime (device);
  atime = bolt_device_get_authtime (device);

  bolt_store_put_times (store, uid, NULL,
                        "conntime", ctime,
                        "authtime", atime,
                        NULL);
  return TRUE;
}

BoltDevice *
bolt_store_get_device (BoltStore  *store,
                       const char *uid,
                       GError    **error)
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
  guint64 atime = 0;
  guint64 ctime = 0;
  guint gen;
  gsize len;

  g_return_val_if_fail (BOLT_IS_STORE (store), NULL);
  g_return_val_if_fail (uid != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

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

  gen = g_key_file_get_int64 (kf, DEVICE_GROUP, "generation", &err);
  if (err != NULL && !bolt_err_notfound (err))
    bolt_warn_err (err, LOG_TOPIC ("store"), "invalid generation");
  g_clear_error (&err);

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

  /* read timestamps, but failing is not fatal */
  bolt_store_get_times (store, uid, NULL,
                        "conntime", &ctime,
                        "authtime", &atime,
                        NULL);

  return g_object_new (BOLT_TYPE_DEVICE,
                       "uid", uid,
                       "name", name,
                       "vendor", vendor,
                       "generation", gen,
                       "type", type,
                       "status", BOLT_STATUS_DISCONNECTED,
                       "store", store,
                       "policy", policy,
                       "key", key,
                       "storetime", stime,
                       "conntime", ctime,
                       "authtime", atime,
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

  g_return_val_if_fail (BOLT_IS_STORE (store), FALSE);
  g_return_val_if_fail (uid != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  devpath = g_file_get_child (store->devices, uid);
  ok = g_file_delete (devpath, NULL, error);

  if (ok)
    g_signal_emit (store, signals[SIGNAL_DEVICE_REMOVED], 0, uid);

  return ok;
}

gboolean
bolt_store_get_time (BoltStore  *store,
                     const char *uid,
                     const char *timesel,
                     guint64    *outval,
                     GError    **error)
{
  g_autoptr(GFile) gf = NULL;
  g_autoptr(GFileInfo) info = NULL;
  g_autofree char *fn = NULL;
  gboolean ok;

  g_return_val_if_fail (BOLT_IS_STORE (store), FALSE);
  g_return_val_if_fail (uid != NULL, FALSE);
  g_return_val_if_fail (timesel != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  fn = g_strdup_printf ("%s.%s", uid, timesel);
  gf = g_file_get_child (store->times, fn);

  info = g_file_query_info (gf, "time::modified",
                            G_FILE_QUERY_INFO_NONE,
                            NULL, error);

  ok = info != NULL;
  if (ok && outval != NULL)
    *outval = g_file_info_get_attribute_uint64 (info, "time::modified");

  return ok;
}

gboolean
bolt_store_get_times (BoltStore  *store,
                      const char *uid,
                      GError    **error,
                      ...)
{
  gboolean res = TRUE;
  gboolean ok = TRUE;
  const char *ts;
  va_list args;

  g_return_val_if_fail (BOLT_IS_STORE (store), FALSE);
  g_return_val_if_fail (uid != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  va_start (args, error);
  while ((ts = va_arg (args, const char *)) != NULL)
    {
      g_autoptr(GError) err = NULL;
      guint64 *val = va_arg (args, guint64 *);

      *val = 0;
      ok = bolt_store_get_time (store, uid, ts, val, &err);

      if (!ok && !bolt_err_notfound (err))
        {
          if (error != NULL)
            {
              res = bolt_error_propagate (error, &err);
              break;
            }

          /* error variable NULL, caller doesn't care, we keep going */
          bolt_warn_err (err, LOG_DEV_UID (uid), LOG_TOPIC ("store"),
                         "failed to read timestamp '%s'", ts);
        }
    }
  va_end (args);

  return res;
}

gboolean
bolt_store_put_time (BoltStore  *store,
                     const char *uid,
                     const char *timesel,
                     guint64     val,
                     GError    **error)
{
  g_autoptr(GFile) gf = NULL;
  g_autofree char *fn = NULL;
  gboolean ok;

  g_return_val_if_fail (BOLT_IS_STORE (store), FALSE);
  g_return_val_if_fail (uid != NULL, FALSE);
  g_return_val_if_fail (timesel != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  fn = g_strdup_printf ("%s.%s", uid, timesel);
  gf = g_file_get_child (store->times, fn);

  ok = bolt_fs_make_parent_dirs (gf, error);
  if (!ok)
    return FALSE;

  ok = bolt_fs_touch (gf, val, val, error);

  return ok;
}

gboolean
bolt_store_put_times (BoltStore  *store,
                      const char *uid,
                      GError    **error,
                      ...)
{
  gboolean res = TRUE;
  gboolean ok = TRUE;
  const char *ts;
  va_list args;

  if (store == NULL)
    {
      g_set_error (error, BOLT_ERROR, BOLT_ERROR_FAILED,
                   "device '%s' is not stored", uid);
      return FALSE;
    }

  g_return_val_if_fail (BOLT_IS_STORE (store), FALSE);
  g_return_val_if_fail (uid != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  va_start (args, error);
  while ((ts = va_arg (args, const char *)) != NULL)
    {
      g_autoptr(GError) err = NULL;
      guint64 val = va_arg (args, guint64);

      if (val == 0)
        continue;

      ok = bolt_store_put_time (store,
                                uid,
                                ts,
                                val,
                                &err);

      if (!ok)
        {
          if (error != NULL)
            {
              res = bolt_error_propagate (error, &err);
              break;
            }

          /* error variable NULL, caller doesn't care, we keep going */
          bolt_warn_err (err, LOG_DEV_UID (uid), LOG_TOPIC ("store"),
                         "failed to update timestamp '%s'", ts);
        }
    }
  va_end (args);

  return res;
}

gboolean
bolt_store_del_time (BoltStore  *store,
                     const char *uid,
                     const char *timesel,
                     GError    **error)
{
  g_autoptr(GFile) pathfile = NULL;
  g_autofree char *name = NULL;
  gboolean ok;

  g_return_val_if_fail (BOLT_IS_STORE (store), FALSE);
  g_return_val_if_fail (uid != NULL, FALSE);
  g_return_val_if_fail (timesel != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  name = g_strdup_printf ("%s.%s", uid, timesel);
  pathfile = g_file_get_child (store->times, name);
  ok = g_file_delete (pathfile, NULL, error);

  return ok;
}

gboolean
bolt_store_del_times (BoltStore  *store,
                      const char *uid,
                      GError    **error,
                      ...)
{
  gboolean res = TRUE;
  gboolean ok = TRUE;
  const char *ts;
  va_list args;

  g_return_val_if_fail (BOLT_IS_STORE (store), FALSE);
  g_return_val_if_fail (uid != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  va_start (args, error);
  while ((ts = va_arg (args, const char *)) != NULL)
    {
      g_autoptr(GError) err = NULL;

      ok = bolt_store_del_time (store, uid, ts, &err);

      if (!ok && !bolt_err_notfound (err))
        {
          if (error != NULL)
            {
              res = bolt_error_propagate (error, &err);
              break;
            }

          /* error variable NULL, caller doesn't care, we keep going */
          bolt_warn_err (err, LOG_DEV_UID (uid), LOG_TOPIC ("store"),
                         "failed to delete timestamp '%s'", ts);
        }
    }
  va_end (args);

  return res;
}


gboolean
bolt_store_put_key (BoltStore  *store,
                    const char *uid,
                    BoltKey    *key,
                    GError    **error)
{
  g_autoptr(GFile) keypath = NULL;
  gboolean ok;

  g_return_val_if_fail (BOLT_IS_STORE (store), FALSE);
  g_return_val_if_fail (uid != NULL, FALSE);
  g_return_val_if_fail (BOLT_IS_KEY (key), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  keypath = g_file_get_child (store->keys, uid);
  ok = bolt_fs_make_parent_dirs (keypath, error);

  if (ok)
    ok = bolt_key_save_file (key, keypath, error);

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

  g_return_val_if_fail (BOLT_IS_STORE (store), key);
  g_return_val_if_fail (uid != NULL, key);

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

  g_return_val_if_fail (BOLT_IS_STORE (store), NULL);
  g_return_val_if_fail (uid != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

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

  g_return_val_if_fail (BOLT_IS_STORE (store), FALSE);
  g_return_val_if_fail (uid != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

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

  g_return_val_if_fail (BOLT_IS_STORE (store), FALSE);
  g_return_val_if_fail (BOLT_IS_DEVICE (dev), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

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

  if (!ok)
    return FALSE;

  bolt_store_del_times (store, uid, NULL,
                        "conntime", "authtime",
                        NULL);

  g_object_set (dev,
                "store", NULL,
                "key", BOLT_KEY_MISSING,
                "policy", BOLT_POLICY_DEFAULT,
                NULL);

  return ok;
}

BoltJournal *
bolt_store_open_journal (BoltStore  *store,
                         const char *type,
                         const char *name,
                         GError    **error)
{
  g_autoptr(GFile) root = NULL;
  BoltJournal *journal;

  g_return_val_if_fail (store != NULL, NULL);
  g_return_val_if_fail (type != NULL, NULL);
  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  root = g_file_get_child (store->root, type);

  journal = bolt_journal_new (root, name, error);

  return journal;
}

gboolean
bolt_store_del_journal (BoltStore  *store,
                        const char *type,
                        const char *name,
                        GError    **error)
{
  g_autoptr(GError) err = NULL;
  g_autoptr(GFile) root = NULL;
  g_autoptr(GFile) journal = NULL;
  gboolean ok;

  g_return_val_if_fail (store != NULL, FALSE);
  g_return_val_if_fail (type != NULL, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  root = g_file_get_child (store->root, type);
  journal = g_file_get_child (root, name);

  ok = g_file_delete (journal, NULL, &err);

  if (!ok && !bolt_err_notfound (err))
    {
      bolt_error_propagate (error, &err);
      return FALSE;
    }

  return TRUE;
}

gboolean
bolt_store_has_journal (BoltStore  *store,
                        const char *type,
                        const char *name)
{
  g_autoptr(GFile) root = NULL;
  g_autoptr(GFile) journal = NULL;


  g_return_val_if_fail (store != NULL, FALSE);
  g_return_val_if_fail (type != NULL, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);

  root = g_file_get_child (store->root, type);
  journal = g_file_get_child (root, name);

  return g_file_query_exists (journal, NULL);
}

gboolean
bolt_store_upgrade (BoltStore *store,
                    gboolean  *upgrade,
                    GError   **error)
{
  g_autoptr(DIR) root = NULL;
  g_autofree char *path = NULL;
  gboolean need_upgrade;
  gboolean ok;

  g_return_val_if_fail (BOLT_IS_STORE (store), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  need_upgrade = store->version != BOLT_STORE_VERSION;

  if (upgrade)
    *upgrade = need_upgrade;

  if (!need_upgrade)
    return TRUE;

  path = g_file_get_path (store->root);
  root = bolt_opendir (path, error);

  if (!root)
    return FALSE;

  ok = bolt_write_int_at (dirfd (root),
                          ".version-upgrade",
                          BOLT_STORE_VERSION,
                          error);
  if (!ok)
    return FALSE;

  ok = bolt_renameat (dirfd (root),
                      ".version-upgrade",
                      dirfd (root),
                      "version",
                      error);

  if (!ok)
    {
      bolt_unlink_at (dirfd (root), ".version-upgrade", 0, NULL);
      return FALSE;
    }

  store->version = BOLT_STORE_VERSION;
  g_object_notify_by_pspec (G_OBJECT (store),
                            store_props[PROP_VERSION]);

  return ok;
}

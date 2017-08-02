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


#include <gio/gfiledescriptorbased.h>
#include <gio/gio.h>
#include <glib.h>
#include <gudev/gudev.h>

#include <stdio.h>
#include <string.h>

#include "device.h"
#include "ioutils.h"
#include "store.h"

struct _TbStoreClass
{
  GObjectClass parent_class;

  gpointer     padding[13];
};

struct _TbStore
{
  GObject  object;

  GFile   *root;
  GFile   *devices;
  GFile   *keys;

  gboolean monitor;
};

enum { PROP_0,

       PROP_ROOT,

       PROP_LAST };

static GParamSpec *props[PROP_LAST] = {
  NULL,
};

G_DEFINE_TYPE (TbStore, tb_store, G_TYPE_OBJECT);

static void
tb_store_finalize (GObject *object)
{
  TbStore *store = TB_STORE (object);

  g_object_unref (store->root);
  g_object_unref (store->devices);
  g_object_unref (store->keys);

  G_OBJECT_CLASS (tb_store_parent_class)->finalize (object);
}

static void
tb_store_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  TbStore *dev = TB_STORE (object);

  switch (prop_id)
    {
    case PROP_ROOT:
      g_value_set_object (value, dev->root);
      break;

    default:
      g_assert_not_reached ();
    }
}

static void
tb_store_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  TbStore *dev = TB_STORE (object);

  switch (prop_id)
    {
    case PROP_ROOT:
      dev->root = g_value_dup_object (value);
      break;

    default:
      g_assert_not_reached ();
    }
}

static void
tb_store_constructed (GObject *obj)
{
  TbStore *store = TB_STORE (obj);

  store->devices = g_file_get_child (store->root, "devices");
  store->keys    = g_file_get_child (store->root, "keys");
}

static void
tb_store_init (TbStore *store)
{
}

static void
tb_store_class_init (TbStoreClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = tb_store_finalize;

  gobject_class->get_property = tb_store_get_property;
  gobject_class->set_property = tb_store_set_property;
  gobject_class->constructed  = tb_store_constructed;

  props[PROP_ROOT] = g_param_spec_object ("root",
                                          NULL,
                                          NULL,
                                          G_TYPE_FILE,
                                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_NAME);

  g_object_class_install_properties (gobject_class, PROP_LAST, props);
}

#if 0
static gboolean
tb_store_write_string (GFile * dir,
                       const char *name, const char *value, GError **error)
{
  g_autoptr(GFile) f = g_file_get_child (dir, name);
  char buffer[1024];
  gboolean ok;
  gint n;

  n = g_snprintf (buffer, sizeof (buffer), "%s\n", value);

  ok = g_file_replace_contents (f,
                                name,
                                n, NULL, FALSE, 0, NULL, NULL, error);

  return ok;
}

static gboolean
tb_store_write_int (GFile * dir, const char *name, int value, GError **error)
{
  g_autoptr(GFile) f = g_file_get_child (dir, name);
  char buffer[1024];
  gboolean ok;
  gint n;

  n = g_snprintf (buffer, sizeof (buffer), "%d\n", value);

  ok = g_file_replace_contents (f,
                                name,
                                n, NULL, FALSE, 0, NULL, NULL, error);

  return ok;
}

static gboolean
tb_store_read_string (GFile * dir,
                      const char *name, char **field, GError **error)
{
  g_autoptr(GFile) f = g_file_get_child (dir, name);
  gboolean ok;
  char *contents;
  gsize len;

  ok = g_file_load_contents (f, NULL, &contents, &len, NULL, error);

  if (!ok)
    return FALSE;

  while (len > 0)
    {
      char c = contents[len - 1];
      if (!g_ascii_isspace (c) || c != '\n')
        break;
      contents[--len] = '\0';
    }

  *field = contents;
  return TRUE;
}
#endif

TbStore *
tb_store_new (const char *path)
{
  g_autoptr(GFile) root = g_file_new_for_path (path);
  TbStore *store;

  store = g_object_new (TB_TYPE_STORE, "root", root, NULL);

  return store;
}

#define DEVICE_GROUP "device"
#define USER_GROUP "user"

gboolean
tb_store_have (TbStore *store, const char *uid)
{
  g_autoptr(GFile) db = NULL;

  g_return_val_if_fail (store != NULL, FALSE);
  g_return_val_if_fail (uid != NULL, FALSE);

  db = g_file_get_child (store->devices, uid);
  return g_file_query_exists (db, NULL);
}

gboolean
tb_store_put (TbStore *store, TbDevice *device, GError **error)
{
  g_autoptr(GError) err  = NULL;
  g_autoptr(GFile) entry = NULL;
  g_autoptr(GKeyFile) kf = NULL;
  g_autofree char *data  = NULL;
  const char *uid;
  TbPolicy policy;
  gboolean ok;
  gsize len;

  g_return_val_if_fail (store != NULL, FALSE);
  g_return_val_if_fail (device != NULL, FALSE);

  uid = tb_device_get_uid (device);
  g_assert (uid);

  ok = g_file_make_directory_with_parents (store->devices, NULL, &err);

  if (!ok && !g_error_matches (err, G_IO_ERROR, G_IO_ERROR_EXISTS))
    {
      g_propagate_error (error, err);
      return FALSE;
    }

  kf = g_key_file_new ();

  g_key_file_set_string (kf, DEVICE_GROUP, "name", tb_device_get_name (device));

  g_key_file_set_string (kf, DEVICE_GROUP, "vendor-name", tb_device_get_vendor_name (device));

  policy = tb_device_get_policy (device);
  if (policy != TB_POLICY_UNKNOWN)
    {
      g_autofree char *str = tb_policy_to_string (policy);

      g_key_file_set_string (kf, USER_GROUP, "policy", str);
    }

  data = g_key_file_to_data (kf, &len, error);

  if (!data)
    return FALSE;

  entry = g_file_get_child (store->devices, uid);

  ok = g_file_replace_contents (entry, data, len, NULL, FALSE, 0, NULL, NULL, error);

  return ok;
}

static GKeyFile *
load_device_data (TbStore *store, const char *uid, GError **error)
{
  g_autoptr(GKeyFile) kf = NULL;
  g_autoptr(GFile) db    = NULL;
  g_autofree char *data  = NULL;
  gboolean ok;
  gsize len;

  g_assert (uid);

  db = g_file_get_child (store->devices, uid);
  ok = g_file_load_contents (db, NULL, &data, &len, NULL, error);

  if (!ok)
    return NULL;

  kf = g_key_file_new ();

  ok = g_key_file_load_from_data (kf, data, len, G_KEY_FILE_NONE, error);

  if (!ok)
    return NULL;

  return g_key_file_ref (kf);
}

static void
load_user_data (TbDevice *dev, GKeyFile *kf)
{
  g_autofree char *policy = NULL;

  policy = g_key_file_get_string (kf, USER_GROUP, "policy", NULL);

  g_object_set (dev, "policy", tb_policy_from_string (policy), "known", TRUE, NULL);
}

gboolean
tb_store_merge (TbStore *store, TbDevice *dev, GError **error)
{
  g_autoptr(GKeyFile) kf = NULL;
  const char *uid        = NULL;

  g_return_val_if_fail (store != NULL, FALSE);
  g_return_val_if_fail (dev != NULL, FALSE);
  uid = tb_device_get_uid (dev);

  kf = load_device_data (store, uid, error);
  if (kf == NULL)
    return FALSE;

  load_user_data (dev, kf);
  return TRUE;
}

TbDevice *
tb_store_get (TbStore *store, const char *uid, GError **error)
{
  g_autoptr(GKeyFile) kf       = NULL;
  g_autofree char *device_name = NULL;
  g_autofree char *vendor_name = NULL;
  g_autofree char *policy      = NULL;

  g_return_val_if_fail (store != NULL, FALSE);
  g_return_val_if_fail (uid != NULL, FALSE);

  kf = load_device_data (store, uid, error);
  if (!kf)
    return NULL;

  device_name = g_key_file_get_string (kf, DEVICE_GROUP, "name", NULL);
  vendor_name = g_key_file_get_string (kf, DEVICE_GROUP, "vendor-name", NULL);
  policy      = g_key_file_get_string (kf, USER_GROUP, "policy", NULL);

  g_assert (device_name);
  g_assert (vendor_name);

  return g_object_new (TB_TYPE_DEVICE,
                       "uid",
                       uid,
                       "device-name",
                       device_name,
                       "vendor-name",
                       vendor_name,
                       "policy",
                       tb_policy_from_string (policy),
                       "known",
                       TRUE,
                       NULL);
}

int
tb_store_create_key (TbStore *store, TbDevice *device, GError **error)
{
  g_autoptr(GError) err                = NULL;
  g_autoptr(GFile) keyfile             = NULL;
  g_autoptr(GFileOutputStream) ostream = NULL;
  g_autoptr(GFile) urnd                = NULL;
  g_autoptr(GFileInputStream) istream  = NULL;
  g_autoptr(GOutputStream) os          = NULL;
  guint8 buffer[TB_KEY_BYTES]          = {
    0,
  };
  char pathbuf[1024] = {
    0,
  };
  const char *uid;
  gboolean ok;
  gsize i, n = 0;
  int fd;

  g_return_val_if_fail (store != NULL, -1);
  g_return_val_if_fail (device != NULL, -1);

  uid     = tb_device_get_uid (device);
  keyfile = g_file_get_child (store->keys, uid);

  ok = g_file_make_directory_with_parents (store->keys, NULL, &err);

  if (!ok && !g_error_matches (err, G_IO_ERROR, G_IO_ERROR_EXISTS))
    {
      g_propagate_error (error, err);
      return -1;
    }

  ostream = g_file_replace (keyfile, NULL, FALSE, G_FILE_CREATE_PRIVATE, NULL, error);

  if (ostream == NULL)
    return -1;

  os = g_buffered_output_stream_new (G_OUTPUT_STREAM (ostream));

  urnd = g_file_new_for_path ("/dev/urandom");

  istream = g_file_read (urnd, NULL, error);

  if (istream == NULL)
    return -1;

  ok = g_input_stream_read_all (G_INPUT_STREAM (istream), buffer, sizeof (buffer), &n, NULL, error);
  if (!ok)
    return -1;

  for (i = 0; i < n; i++)
    {
      char buf[3];
      int count;

      count = g_snprintf (buf, sizeof (buf), "%02hhx", buffer[i]);
      if (count != 2)
        {
          g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_FAILED, "could not format key data");
          ok = FALSE;
          break;
        }

      ok = g_output_stream_write_all (os, buf, count, NULL, NULL, error);
      if (!ok)
        break;
    }

  if (!ok)
    return -1;

  fd = g_file_descriptor_based_get_fd (G_FILE_DESCRIPTOR_BASED (ostream));
  g_snprintf (pathbuf, sizeof (pathbuf), "/proc/self/fd/%d", fd);
  // TODO: check return value from g_snprintf

  fd = tb_open (pathbuf, O_RDONLY, error);

  return fd;
}

gint
tb_store_open_key (TbStore *store, const char *uid, GError **error)
{
  g_autoptr(GFile) keyfile = NULL;
  g_autofree char *path    = NULL;

  g_return_val_if_fail (store != NULL, FALSE);
  g_return_val_if_fail (uid != NULL, FALSE);

  keyfile = g_file_get_child (store->keys, uid);
  path    = g_file_get_path (keyfile);

  return tb_open (path, O_RDONLY, error);
}

gboolean
tb_store_have_key (TbStore *store, const char *uid)
{
  g_autoptr(GFile) keyfile = NULL;

  g_return_val_if_fail (store != NULL, FALSE);
  g_return_val_if_fail (uid != NULL, FALSE);

  keyfile = g_file_get_child (store->keys, uid);

  return g_file_query_exists (keyfile, NULL);
}

GStrv
tb_store_list_ids (TbStore *store, GError **error)
{
  GPtrArray *ids;

  g_autoptr(GDir) dir   = NULL;
  g_autofree char *path = NULL;
  const char *name;

  path = g_file_get_path (store->devices);

  dir = g_dir_open (path, 0, error);
  if (dir == NULL)
    return NULL;

  ids = g_ptr_array_new ();

  while ((name = g_dir_read_name (dir)) != NULL)
    {
      if (g_str_has_prefix (name, "."))
        continue;

      g_ptr_array_add (ids, g_strdup (name));
    }

  g_ptr_array_add (ids, NULL);
  return (GStrv) g_ptr_array_free (ids, FALSE);
}

gboolean
tb_store_delete (TbStore *store, const char *uid, GError **error)
{
  g_autoptr(GFile) data      = NULL;
  g_autoptr(GFile) key       = NULL;
  g_autoptr(GError) err_data = NULL;
  g_autoptr(GError) err_key  = NULL;
  gboolean ok_data;
  gboolean ok_key;

  g_return_val_if_fail (store != NULL, FALSE);
  g_return_val_if_fail (uid != NULL, FALSE);

  data = g_file_get_child (store->devices, uid);
  key  = g_file_get_child (store->keys, uid);

  ok_data = g_file_delete (data, NULL, &err_data);
  ok_key  = g_file_delete (key, NULL, &err_key);

  if (!ok_key && g_error_matches (err_key, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
    ok_key = TRUE;

  if (ok_data && ok_key)
    return TRUE;

  if (!ok_data && !ok_key)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_FAILED,
                   "Could not remove device data (%s) and key (%s)",
                   err_data->message,
                   err_key->message);
    }
  else
    {
      g_propagate_error (error, ok_key ? err_data : err_key);
    }

  return FALSE;
}

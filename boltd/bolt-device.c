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

#include "bolt-dbus.h"
#include "bolt-device.h"
#include "bolt-enums.h"
#include "bolt-error.h"
#include "bolt-io.h"
#include "bolt-manager.h"
#include "bolt-store.h"
#include "bolt-str.h"

#include <dirent.h>
#include <libudev.h>

/* dbus method calls */
static gboolean    handle_authorize (BoltDBusDevice        *object,
                                     GDBusMethodInvocation *invocation,
                                     gpointer               user_data);


struct _BoltDevice
{
  BoltDBusDeviceSkeleton object;

  /* device props */
  char      *dbus_path;

  char      *uid;
  char      *name;
  char      *vendor;

  BoltStatus status;

  /* when device is attached */
  char        *syspath;
  BoltSecurity security;
  char        *parent;

  /* when device is stored */
  BoltStore   *store;
  BoltPolicy   policy;
  BoltKeyState key;
};


enum {
  PROP_0,

  PROP_STORE,

  PROP_LAST,

  /* overridden */
  PROP_UID,
  PROP_NAME,
  PROP_VENDOR,
  PROP_STATUS,

  PROP_PARENT,
  PROP_SYSFS,
  PROP_SECURITY,

  PROP_STORED,
  PROP_POLICY,
  PROP_HAVE_KEY,
};

static GParamSpec *props[PROP_LAST] = {NULL, };

enum {
  SIGNAL_STATUS_CHANGED,
  SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = {0};

G_DEFINE_TYPE (BoltDevice,
               bolt_device,
               BOLT_DBUS_TYPE_DEVICE_SKELETON)

static void
bolt_device_finalize (GObject *object)
{
  BoltDevice *dev = BOLT_DEVICE (object);

  g_clear_object (&dev->store);

  g_free (dev->dbus_path);

  g_free (dev->uid);
  g_free (dev->name);
  g_free (dev->vendor);

  g_free (dev->parent);
  g_free (dev->syspath);

  G_OBJECT_CLASS (bolt_device_parent_class)->finalize (object);
}

static void
bolt_device_init (BoltDevice *dev)
{
  g_signal_connect (dev, "handle-authorize",
                    G_CALLBACK (handle_authorize), NULL);
}

static void
bolt_device_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  BoltDevice *dev = BOLT_DEVICE (object);

  switch (prop_id)
    {
    case PROP_STORE:
      g_value_set_object (value, dev->store);
      break;

    case PROP_UID:
      g_value_set_string (value, dev->uid);
      break;

    case PROP_NAME:
      g_value_set_string (value, dev->name);
      break;

    case PROP_VENDOR:
      g_value_set_string (value, dev->vendor);
      break;

    case PROP_STATUS:
      g_value_set_uint (value, dev->status);
      break;

    case PROP_PARENT:
      g_value_set_string (value, dev->parent);
      break;

    case PROP_SYSFS:
      g_value_set_string (value, dev->syspath);
      break;

    case PROP_SECURITY:
      g_value_set_uint (value, dev->security);
      break;

    case PROP_STORED:
      g_value_set_boolean (value, dev->store != NULL);
      break;

    case PROP_POLICY:
      g_value_set_uint (value, dev->policy);
      break;

    case PROP_HAVE_KEY:
      g_value_set_uint (value, dev->key);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bolt_device_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  BoltDevice *dev = BOLT_DEVICE (object);
  GParamSpec *parent;

  switch (prop_id)
    {
    case PROP_STORE:
      dev->store = g_value_dup_object (value);
      g_object_notify (object, "stored");
      break;

    case PROP_UID:
      g_return_if_fail (dev->uid == NULL);
      dev->uid = g_value_dup_string (value);
      break;

    case PROP_NAME:
      g_clear_pointer (&dev->name, g_free);
      dev->name = g_value_dup_string (value);
      break;

    case PROP_VENDOR:
      g_clear_pointer (&dev->vendor, g_free);
      dev->vendor = g_value_dup_string (value);
      break;

    case PROP_STATUS:
      {
        BoltStatus old = dev->status;
        BoltStatus now = g_value_get_uint (value);
        if (old == now)
          break;

        dev->status = now;
        g_signal_emit (dev,
                       signals[SIGNAL_STATUS_CHANGED],
                       0,
                       old);
        break;
      }

    case PROP_PARENT:
      g_clear_pointer (&dev->parent, g_free);
      dev->parent = g_value_dup_string (value);
      break;

    case PROP_SYSFS:
      g_clear_pointer (&dev->syspath, g_free);
      dev->syspath = g_value_dup_string (value);
      break;

    case PROP_SECURITY:
      dev->security = g_value_get_uint (value);
      break;

    case PROP_POLICY:
      dev->policy = g_value_get_uint (value);
      break;

    case PROP_HAVE_KEY:
      dev->key = g_value_get_uint (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }

  /* This is one gross hack to make the DBus property change
   * signal emission work.
   *   The heart "problem" is a combination of two things:
   *   1) the dbus object properties are override here,
   *      which means the property setter from the parent
   *      (the dbus skeleton) will not be called.
   *   2) The dbus skeleton will only emit the signal on the
   *      bus if it finds that the value has changed, which
   *      it can only detect if its property setter was called
   *   => We need to call the skeleton's set_property function
   *      from here, so it can detect property changes itself.
   */

  /* all properties below PROP_LAST are our own, non-dbus props */
  if (prop_id < PROP_LAST)
    return;

  /* we need to original GParamSpec, since the one we got is
   * the new, overridden one */
  parent = g_object_class_find_property (bolt_device_parent_class,
                                         pspec->name);
  if (parent == NULL)
    return;

  /* the prop_id is *our* prop_id, not the parent's prop_id, which
   * we need. For this to work our property list *after* PROP_LAST
   * need to be in sync with the parent */
  prop_id -= PROP_LAST;

  G_OBJECT_CLASS (bolt_device_parent_class)->set_property (object,
                                                           prop_id,
                                                           value,
                                                           parent);
}


static void
bolt_device_class_init (BoltDeviceClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = bolt_device_finalize;

  gobject_class->get_property = bolt_device_get_property;
  gobject_class->set_property = bolt_device_set_property;

  props[PROP_STORE] =
    g_param_spec_object ("store",
                         NULL, NULL,
                         BOLT_TYPE_STORE,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_NICK);

  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     props);

  g_object_class_override_property (gobject_class,
                                    PROP_UID,
                                    "uid");

  g_object_class_override_property (gobject_class,
                                    PROP_NAME,
                                    "name");

  g_object_class_override_property (gobject_class,
                                    PROP_VENDOR,
                                    "vendor");

  g_object_class_override_property (gobject_class,
                                    PROP_STATUS,
                                    "status");

  g_object_class_override_property (gobject_class,
                                    PROP_PARENT,
                                    "parent");

  g_object_class_override_property (gobject_class,
                                    PROP_SYSFS,
                                    "sysfs-path");

  g_object_class_override_property (gobject_class,
                                    PROP_SECURITY,
                                    "security");

  g_object_class_override_property (gobject_class,
                                    PROP_STORED,
                                    "stored");

  g_object_class_override_property (gobject_class,
                                    PROP_POLICY,
                                    "policy");

  g_object_class_override_property (gobject_class,
                                    PROP_HAVE_KEY,
                                    "key");


  signals[SIGNAL_STATUS_CHANGED] =
    g_signal_new ("status-changed",
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE,
                  1, BOLT_TYPE_STATUS);

}

/* internal methods */

static const char *
read_sysattr_name (struct udev_device *udev, const char *attr, GError **error)
{
  g_autofree char *s = NULL;
  const char *v;

  s = g_strdup_printf ("%s_name", attr);
  v = udev_device_get_sysattr_value (udev, s);

  if (v != NULL)
    return v;

  v = udev_device_get_sysattr_value (udev, attr);

  if (v == NULL)
    g_set_error (error,
                 BOLT_ERROR, BOLT_ERROR_UDEV,
                 "failed to get sysfs attr: %s", attr);

  return v;
}

static gint
read_sysfs_attr_int (struct udev_device *device, const char *attr)
{
  const char *str;
  char *end;
  gint64 val;

  str = udev_device_get_sysattr_value (device, attr);

  if (str == NULL)
    return 0;

  val = g_ascii_strtoll (str, &end, 0);

  if (str == end)
    return 0;

  if (val > G_MAXINT || val < G_MININT)
    {
      g_warning ("value read from sysfs outside of gint's range.");
      val = 0;
    }

  return (gint) val;
}

static gboolean
string_nonzero (const char *str)
{
  return str != NULL && str[0] != '\0';
}

static const char *
bolt_sysfs_get_parent_uid (struct udev_device *udev)
{
  struct udev_device *parent;
  const char *uid = NULL;

  parent = udev_device_get_parent (udev);
  if (parent)
    uid = udev_device_get_sysattr_value (parent, "unique_id");
  return uid;
}

static BoltStatus
bolt_status_from_udev (struct udev_device *udev)
{
  gint authorized;
  const char *key;
  gboolean have_key;

  authorized = read_sysfs_attr_int (udev, "authorized");

  if (authorized == 2)
    return BOLT_STATUS_AUTHORIZED_SECURE;

  key = udev_device_get_sysattr_value (udev, "key");
  have_key = string_nonzero (key);

  if (authorized == 1)
    {
      if (have_key)
        return BOLT_STATUS_AUTHORIZED_NEWKEY;
      else
        return BOLT_STATUS_AUTHORIZED;
    }
  else if (authorized == 0 && have_key)
    {
      return BOLT_STATUS_AUTH_ERROR;
    }

  return BOLT_STATUS_CONNECTED;
}


static struct udev_device *
bolt_sysfs_domain_for_device (struct udev_device *udev)
{
  struct udev_device *parent;
  gboolean found;

  found = FALSE;
  parent = udev;
  do
    {
      const char *devtype;
      parent = udev_device_get_parent (parent);
      if (!parent)
        break;

      devtype = udev_device_get_devtype (parent);
      found = bolt_streq (devtype, "thunderbolt_domain");

    }
  while (!found);

  return found ? parent : NULL;
}

static BoltSecurity
bolt_sysfs_security_for_device (struct udev_device *udev)
{
  struct udev_device *parent = NULL;
  const char *v;
  BoltSecurity s;

  parent = bolt_sysfs_domain_for_device (udev);
  if (parent == NULL)
    {
      g_warning ("Failed to determine domain device");
      return BOLT_SECURITY_NONE;
    }

  v = udev_device_get_sysattr_value (parent, "security");
  s = bolt_security_from_string (v);

  if (!bolt_security_validate (s))
    {
      g_warning ("invalid security: %s", v);
      s = BOLT_SECURITY_NONE;
    }

  return s;
}

/*  device authorization */

typedef struct
{
  BoltAuth *auth;

  /* the outer callback  */
  GAsyncReadyCallback callback;
  gpointer            user_data;

} AuthData;

static void
auth_data_free (gpointer data)
{
  AuthData *auth = data;

  g_debug ("freeing auth data");
  g_clear_object (&auth->auth);
  g_slice_free (AuthData, auth);
}

static gboolean
authorize_device_internal (BoltDevice *dev,
                           BoltAuth   *auth,
                           GError    **error)
{
  g_autoptr(DIR) devdir = NULL;
  BoltKey *key;
  BoltSecurity level;
  gboolean ok;

  key = bolt_auth_get_key (auth);
  level = bolt_auth_get_level (auth);

  devdir = bolt_opendir (dev->syspath, error);
  if (devdir == NULL)
    return FALSE;

  ok = bolt_verify_uid (dirfd (devdir), dev->uid, error);
  if (!ok)
    return FALSE;

  if (key)
    {
      int keyfd;

      g_debug ("[%s] writing key", dev->uid);
      keyfd = bolt_openat (dirfd (devdir), "key", O_WRONLY | O_CLOEXEC, error);
      if (keyfd < 0)
        return FALSE;

      ok = bolt_key_write_to (key, keyfd, &level, error);
      close (keyfd);
      if (!ok)
        return FALSE;
    }

  g_debug ("[%s] writing authorization", dev->uid);
  ok = bolt_write_char_at (dirfd (devdir),
                           "authorized",
                           level,
                           error);
  return ok;
}

static void
authorize_in_thread (GTask        *task,
                     gpointer      source,
                     gpointer      context,
                     GCancellable *cancellable)
{
  g_autoptr(GError) error = NULL;
  BoltDevice *dev = source;
  AuthData *auth_data = context;
  BoltAuth *auth = auth_data->auth;
  gboolean ok;

  ok = authorize_device_internal (dev, auth, &error);

  if (!ok)
    g_task_return_new_error (task, BOLT_ERROR, BOLT_ERROR_FAILED,
                             "failed to authorize device: %s",
                             error->message);
  else
    g_task_return_boolean (task, TRUE);
}

static void
authorize_thread_done (GObject      *object,
                       GAsyncResult *res,
                       gpointer      user_data)
{
  g_autoptr(GError) error = NULL;
  BoltDevice *dev = BOLT_DEVICE (object);
  GTask *task = G_TASK (res);
  BoltStatus status;
  AuthData *auth_data;
  BoltAuth *auth;
  gboolean ok;

  auth_data = g_task_get_task_data (task);
  auth = auth_data->auth;

  ok = g_task_propagate_boolean (task, &error);

  if (!ok)
    {
      status = BOLT_STATUS_AUTH_ERROR;
      g_object_set (dev, "status", status, NULL);
      bolt_auth_return_error (auth, &error);
    }

  if (auth_data->callback)
    auth_data->callback (G_OBJECT (dev),
                         G_ASYNC_RESULT (auth),
                         auth_data->user_data);
}

void
bolt_device_authorize (BoltDevice         *dev,
                       BoltAuth           *auth,
                       GAsyncReadyCallback callback,
                       gpointer            user_data)
{
  AuthData *auth_data;
  GTask *task;

  g_object_set (auth, "device", dev, NULL);

  if (dev->status != BOLT_STATUS_CONNECTED &&
      dev->status != BOLT_STATUS_AUTH_ERROR)
    {
      bolt_auth_return_new_error (auth, BOLT_ERROR, BOLT_ERROR_FAILED,
                                  "wrong device state: %u", dev->status);

      if (callback)
        callback (G_OBJECT (dev), G_ASYNC_RESULT (auth), user_data);

      return;
    }

  task = g_task_new (dev, NULL, authorize_thread_done, NULL);
  auth_data = g_slice_new (AuthData);
  auth_data->callback = callback;
  auth_data->user_data = user_data;
  auth_data->auth = g_object_ref (auth);
  g_task_set_task_data (task, auth_data, auth_data_free);

  g_object_set (dev, "status", BOLT_STATUS_AUTHORIZING, NULL);

  g_task_run_in_thread (task, authorize_in_thread);
  g_object_unref (task);
}


/* dbus methods */

static void
handle_authorize_done (GObject      *device,
                       GAsyncResult *res,
                       gpointer      user_data)
{
  GDBusMethodInvocation *inv;
  GError *error = NULL;
  BoltAuth *auth;
  BoltDevice *dev;
  gboolean ok;

  dev = BOLT_DEVICE (device);
  inv  = user_data;
  auth = BOLT_AUTH (res);

  ok = bolt_auth_check (auth, &error);
  if (ok)
    bolt_dbus_device_complete_authorize (BOLT_DBUS_DEVICE (dev), inv);
  else
    g_dbus_method_invocation_take_error (inv, error);
}

static gboolean
handle_authorize (BoltDBusDevice        *object,
                  GDBusMethodInvocation *inv,
                  gpointer               user_data)
{
  BoltDevice *dev = BOLT_DEVICE (object);
  GError *error = NULL;
  BoltAuth *auth;
  BoltSecurity level;
  BoltKey *key;

  level = dev->security;
  key = NULL;

  if (level == BOLT_SECURITY_SECURE)
    {
      if (dev->key)
        key = bolt_store_get_key (dev->store, dev->uid, &error);
      else
        level = BOLT_SECURITY_USER;
    }

  if (level == BOLT_SECURITY_SECURE && key == NULL)
    {
      g_dbus_method_invocation_take_error (inv, error);
      return TRUE;
    }

  auth = bolt_auth_new (dev, level, key);
  bolt_device_authorize (dev, auth, handle_authorize_done, inv);

  return TRUE;
}

/* public methods */

BoltDevice *
bolt_device_new_for_udev (struct udev_device *udev,
                          GError            **error)
{
  const char *uid;
  const char *name;
  const char *vendor;
  const char *syspath;
  const char *parent;
  BoltDevice *dev;

  uid = udev_device_get_sysattr_value (udev, "unique_id");
  if (udev == NULL)
    {
      g_set_error (error, BOLT_ERROR, BOLT_ERROR_UDEV,
                   "could not get unique_id for udev");
      return NULL;
    }

  syspath = udev_device_get_syspath (udev);
  g_return_val_if_fail (syspath != NULL, NULL);

  name = read_sysattr_name (udev, "device", error);
  if (name == NULL)
    return NULL;

  vendor = read_sysattr_name (udev, "vendor", error);
  if (vendor == NULL)
    return NULL;

  parent = bolt_sysfs_get_parent_uid (udev);
  dev = g_object_new (BOLT_TYPE_DEVICE,
                      "uid", uid,
                      "name", name,
                      "vendor", vendor,
                      "sysfs-path", syspath,
                      "parent", parent,
                      NULL);

  dev->status = bolt_status_from_udev (udev);
  dev->security = bolt_sysfs_security_for_device (udev);

  return dev;
}

const char *
bolt_device_export (BoltDevice      *device,
                    GDBusConnection *connection,
                    GError         **error)
{
  const char *path;
  gboolean ok;

  g_return_val_if_fail (BOLT_IS_DEVICE (device), FALSE);

  path = bolt_device_get_object_path (device);

  ok = g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (device),
                                         connection,
                                         path,
                                         error);
  return ok ? path : NULL;
}

void
bolt_device_unexport (BoltDevice *device)
{
  g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (device));
}


BoltStatus
bolt_device_connected (BoltDevice         *dev,
                       struct udev_device *udev)
{
  const char *syspath;
  const char *parent;
  BoltSecurity security;
  BoltStatus status;

  syspath = udev_device_get_syspath (udev);
  status = bolt_status_from_udev (udev);
  security = bolt_sysfs_security_for_device (udev);
  parent = bolt_sysfs_get_parent_uid (udev);

  g_object_set (G_OBJECT (dev),
                "parent", parent,
                "sysfs-path", syspath,
                "security", security,
                "status", status,
                NULL);

  g_debug ("[%s] parent is %s", dev->uid, dev->parent);

  return status;
}

BoltStatus
bolt_device_disconnected (BoltDevice *dev)
{
  g_object_set (G_OBJECT (dev),
                "parent", NULL,
                "sysfs-path", NULL,
                "security", BOLT_SECURITY_NONE,
                "status", BOLT_STATUS_DISCONNECTED,
                NULL);

  /* check if we have a new key for the device, and
   * if so, change its state to KEY_HAVE, because
   * now it is not new anymore.
   */
  if (dev->key == BOLT_KEY_NEW)
    g_object_set (G_OBJECT (dev), "key", BOLT_KEY_HAVE, NULL);

  return dev->status;
}

gboolean
bolt_device_is_connected (BoltDevice *device)
{
  return bolt_status_is_connected (device->status);
}

BoltStatus
bolt_device_update_from_udev (BoltDevice         *dev,
                              struct udev_device *udev)
{
  BoltStatus status = bolt_status_from_udev (udev);

  g_object_set (G_OBJECT (dev),
                "status", status,
                NULL);

  return status;
}

BoltKeyState
bolt_device_get_keystate (BoltDevice *dev)
{
  return dev->key;
}

const char *
bolt_device_get_name (BoltDevice *dev)
{
  return dev->name;
}

const char *
bolt_device_get_object_path (BoltDevice *device)
{
  g_return_val_if_fail (BOLT_IS_DEVICE (device), FALSE);

  if (device->dbus_path == NULL)
    {
      char *path = NULL;

      path = g_strdup_printf (BOLT_DBUS_PATH "/devices/%s", device->uid);
      g_strdelimit (path, "-", '_');

      device->dbus_path = path;
    }

  return device->dbus_path;
}

BoltPolicy
bolt_device_get_policy (BoltDevice *dev)
{
  return dev->policy;
}

const char *
bolt_device_get_uid (BoltDevice *dev)
{
  return dev->uid;
}

BoltSecurity
bolt_device_get_security (BoltDevice *dev)
{
  return dev->security;
}

BoltStatus
bolt_device_get_status (BoltDevice *dev)
{
  return dev->status;
}

gboolean
bolt_device_get_stored (BoltDevice *dev)
{
  return dev->store != NULL;
}

const char *
bolt_device_get_syspath (BoltDevice *dev)
{
  return dev->syspath;
}

const char *
bolt_device_get_vendor (BoltDevice *dev)
{
  return dev->vendor;
}

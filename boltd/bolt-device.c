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

#include "bolt-device.h"

#include "bolt-domain.h"
#include "bolt-enums.h"
#include "bolt-error.h"
#include "bolt-io.h"
#include "bolt-log.h"
#include "bolt-manager.h"
#include "bolt-names.h"
#include "bolt-store.h"
#include "bolt-str.h"
#include "bolt-sysfs.h"
#include "bolt-time.h"

#include <dirent.h>
#include <libudev.h>

/* internal methods */
static void     device_set_status_internal (BoltDevice *dev,
                                            BoltStatus  status,
                                            gboolean    notify);

/* dbus property setter */
static gboolean handle_set_label (BoltExported *obj,
                                  const char   *name,
                                  const GValue *value,
                                  GError      **error);

/* dbus method calls */
static GVariant *  handle_authorize (BoltExported          *object,
                                     GVariant              *params,
                                     GDBusMethodInvocation *invocation,
                                     GError               **error);


struct _BoltDevice
{
  BoltExported object;

  /* device props */
  char          *dbus_path;

  char          *uid;
  char          *name;
  char          *vendor;

  BoltDeviceType type;
  BoltStatus     status;

  /* when device is attached */
  BoltAuthFlags aflags;
  char         *syspath;
  BoltDomain   *domain;
  char         *parent;

  guint64       conntime;
  guint64       authtime;

  /* when device is stored */
  BoltStore   *store;
  BoltPolicy   policy;
  BoltKeyState key;
  guint64      storetime;

  char        *label;
};


enum {
  PROP_0,

  PROP_STORE,
  PROP_SECURITY,

  /* exported properties start here, */
  PROP_UID,
  PROP_NAME,
  PROP_VENDOR,
  PROP_TYPE,
  PROP_STATUS,

  PROP_AUTHFLAGS,
  PROP_PARENT,
  PROP_SYSFS,
  PROP_DOMAIN,
  PROP_CONNTIME,
  PROP_AUTHTIME,

  PROP_STORED,
  PROP_POLICY,
  PROP_HAVE_KEY,
  PROP_STORETIME,
  PROP_LABEL,

  PROP_LAST,
  PROP_EXPORTED = PROP_UID
};

static GParamSpec *props[PROP_LAST] = {NULL, };

enum {
  SIGNAL_STATUS_CHANGED,
  SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = {0};

G_DEFINE_TYPE (BoltDevice,
               bolt_device,
               BOLT_TYPE_EXPORTED)

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
  g_clear_object (&dev->domain);
  g_free (dev->label);


  G_OBJECT_CLASS (bolt_device_parent_class)->finalize (object);
}

static void
bolt_device_init (BoltDevice *dev)
{
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

    case PROP_SECURITY:
      {
        BoltSecurity sl = BOLT_SECURITY_UNKNOWN;

        if (dev->domain)
          sl = bolt_domain_get_security (dev->domain);

        g_value_set_enum (value, sl);
      }
      break;

    case PROP_UID:
      g_value_set_string (value, dev->uid);
      break;

    case PROP_NAME:
      g_value_set_string (value, dev->name);
      break;

    case PROP_TYPE:
      g_value_set_enum (value, dev->type);
      break;

    case PROP_VENDOR:
      g_value_set_string (value, dev->vendor);
      break;

    case PROP_STATUS:
      g_value_set_enum (value, dev->status);
      break;

    case PROP_AUTHFLAGS:
      g_value_set_flags (value, dev->aflags);
      break;

    case PROP_PARENT:
      g_value_set_string (value, dev->parent);
      break;

    case PROP_SYSFS:
      g_value_set_string (value, dev->syspath);
      break;

    case PROP_DOMAIN:
      g_value_set_object (value, dev->domain);
      break;

    case PROP_CONNTIME:
      g_value_set_uint64 (value, dev->conntime);
      break;

    case PROP_AUTHTIME:
      g_value_set_uint64 (value, dev->authtime);
      break;

    case PROP_STORED:
      g_value_set_boolean (value, dev->store != NULL);
      break;

    case PROP_POLICY:
      g_value_set_enum (value, dev->policy);
      break;

    case PROP_HAVE_KEY:
      g_value_set_enum (value, dev->key);
      break;

    case PROP_STORETIME:
      g_value_set_uint64 (value, dev->storetime);
      break;

    case PROP_LABEL:
      g_value_set_string (value, dev->label);
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

  switch (prop_id)
    {
    case PROP_STORE:
      dev->store = g_value_dup_object (value);
      g_object_notify_by_pspec (object, props[PROP_STORED]);
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

    case PROP_TYPE:
      dev->type = g_value_get_enum (value);
      break;

    case PROP_STATUS:
      {
        BoltStatus now = g_value_get_enum (value);
        device_set_status_internal (dev, now, FALSE);
        break;
      }

    case PROP_AUTHFLAGS:
      dev->aflags = g_value_get_flags (value);
      break;

    case PROP_PARENT:
      g_clear_pointer (&dev->parent, g_free);
      dev->parent = g_value_dup_string (value);
      break;

    case PROP_SYSFS:
      g_clear_pointer (&dev->syspath, g_free);
      dev->syspath = g_value_dup_string (value);
      break;

    case PROP_DOMAIN:
      g_clear_object (&dev->domain);
      dev->domain = g_value_dup_object (value);
      break;

    case PROP_CONNTIME:
      dev->conntime = g_value_get_uint64 (value);
      break;

    case PROP_AUTHTIME:
      dev->authtime = g_value_get_uint64 (value);
      break;

    case PROP_POLICY:
      dev->policy = g_value_get_enum (value);
      break;

    case PROP_HAVE_KEY:
      dev->key = g_value_get_enum (value);
      break;

    case PROP_STORETIME:
      dev->storetime = g_value_get_uint64 (value);
      break;

    case PROP_LABEL:
      g_clear_pointer (&dev->label, g_free);
      dev->label = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}


static void
bolt_device_class_init (BoltDeviceClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  BoltExportedClass *exported_class = BOLT_EXPORTED_CLASS (klass);

  gobject_class->finalize = bolt_device_finalize;

  gobject_class->get_property = bolt_device_get_property;
  gobject_class->set_property = bolt_device_set_property;

  props[PROP_STORE] =
    g_param_spec_object ("store",
                         NULL, NULL,
                         BOLT_TYPE_STORE,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_SECURITY] =
    g_param_spec_enum ("security",
                       "Security", NULL,
                       BOLT_TYPE_SECURITY,
                       BOLT_SECURITY_NONE,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS);

  props[PROP_UID] =
    g_param_spec_string ("uid",
                         "Uid", NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_NAME] =
    g_param_spec_string ("name",
                         "Name", NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_VENDOR] =
    g_param_spec_string ("vendor",
                         "Vendor", NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_TYPE] =
    g_param_spec_enum ("type",
                       "Type", NULL,
                       BOLT_TYPE_DEVICE_TYPE,
                       BOLT_DEVICE_PERIPHERAL,
                       G_PARAM_READWRITE |
                       G_PARAM_CONSTRUCT_ONLY |
                       G_PARAM_STATIC_STRINGS);

  props[PROP_STATUS] =
    g_param_spec_enum ("status",
                       "Status", NULL,
                       BOLT_TYPE_STATUS,
                       BOLT_STATUS_DISCONNECTED,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS);

  props[PROP_AUTHFLAGS] =
    g_param_spec_flags ("authflags",
                        "AuthFlags", NULL,
                        BOLT_TYPE_AUTH_FLAGS,
                        BOLT_AUTH_NONE,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS);

  props[PROP_PARENT] =
    g_param_spec_string ("parent",
                         "Parent", NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_SYSFS] =
    g_param_spec_string ("sysfs-path",
                         "SysfsPath", NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_DOMAIN] =
    g_param_spec_object ("domain",
                         "Domain", NULL,
                         BOLT_TYPE_DOMAIN,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_CONNTIME] =
    g_param_spec_uint64 ("conntime",
                         "ConnectTime", NULL,
                         0, G_MAXUINT64, 0,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_AUTHTIME] =
    g_param_spec_uint64 ("authtime",
                         "AuthorizeTime", NULL,
                         0, G_MAXUINT64, 0,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_STORED] =
    g_param_spec_boolean ("stored",
                          "Stored", NULL,
                          FALSE,
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_STRINGS);

  props[PROP_POLICY] =
    g_param_spec_enum ("policy",
                       "Policy", NULL,
                       BOLT_TYPE_POLICY,
                       BOLT_POLICY_DEFAULT,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS);

  props[PROP_HAVE_KEY] =
    g_param_spec_enum ("key",
                       "Key", NULL,
                       BOLT_TYPE_KEY_STATE,
                       BOLT_KEY_MISSING,
                       G_PARAM_READWRITE |
                       G_PARAM_STATIC_STRINGS);

  props[PROP_STORETIME] =
    g_param_spec_uint64 ("storetime",
                         "StoreTime", NULL,
                         0, G_MAXUINT64, 0,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_LABEL] =
    g_param_spec_string ("label",
                         "Label", NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     props);

  signals[SIGNAL_STATUS_CHANGED] =
    g_signal_new ("status-changed",
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE,
                  1, BOLT_TYPE_STATUS);

  bolt_exported_class_set_interface_info (exported_class,
                                          BOLT_DBUS_DEVICE_INTERFACE,
                                          "/boltd/org.freedesktop.bolt.xml");

  bolt_exported_class_export_properties (exported_class,
                                         PROP_EXPORTED,
                                         PROP_LAST,
                                         props);

  bolt_exported_class_property_setter (exported_class,
                                       props[PROP_LABEL],
                                       handle_set_label);

  bolt_exported_class_export_method (exported_class,
                                     "Authorize",
                                     handle_authorize);

}

/* internal methods */
static void
device_set_status_internal (BoltDevice *dev,
                            BoltStatus  status,
                            gboolean    notify)
{
  BoltStatus before;

  before = dev->status;
  if (before == status)
    return;

  dev->status = status;

  g_signal_emit (dev, signals[SIGNAL_STATUS_CHANGED], 0, before);

  if (notify)
    g_object_notify_by_pspec (G_OBJECT (dev), props[PROP_STATUS]);
}

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

static BoltStatus
bolt_status_from_info (BoltDevInfo *info)
{
  gint authorized;
  gboolean have_key;

  authorized = info->authorized;
  have_key = info->keysize > 0;

  if (authorized < 0)
    return BOLT_STATUS_UNKNOWN;
  else if (authorized > 0)
    return BOLT_STATUS_AUTHORIZED;
  if (have_key) /* authorized == 0 */
    return BOLT_STATUS_AUTH_ERROR;

  return BOLT_STATUS_CONNECTED;
}

static BoltAuthFlags
bolt_auth_flags_from_info (BoltDevInfo   *info,
                           BoltDomain    *domain,
                           BoltAuthFlags *mask)
{
  BoltSecurity sl;
  guint val = 0;
  guint msk = 0;

  g_return_val_if_fail (info != NULL, 0);
  g_return_val_if_fail (domain != NULL, 0);

  msk |= BOLT_AUTH_SECURE;
  if (info->authorized == 2)
    val |= BOLT_AUTH_SECURE;

  sl = bolt_domain_get_security (domain);
  if (sl == BOLT_SECURITY_SECURE)
    {
      msk |= BOLT_AUTH_NOKEY;

      if (info->keysize < 0)
        val |= BOLT_AUTH_NOKEY;
    }

  msk |= BOLT_AUTH_NOPCIE;
  if (!bolt_security_allows_pcie (sl))
    val |= BOLT_AUTH_NOPCIE;

  msk |= BOLT_AUTH_BOOT;
  if (info->boot > 0)
    val |= BOLT_AUTH_BOOT;

  if (mask)
    *mask = msk;

  return val;
}

/*  device authorization */

static gboolean
device_check_parent_auth (BoltDevice *dev,
                          DIR        *devdir,
                          int        *auth)
{

  g_autoptr(DIR) parent = NULL;
  g_autoptr(GError) err = NULL;
  gboolean ok;

  parent = bolt_opendir_at (dirfd (devdir), "..", O_RDONLY, &err);

  if (!parent)
    {
      bolt_warn_err (err, LOG_DEV (dev), LOG_TOPIC ("authorize"),
                     "could not open parent directory of device");
      return FALSE;
    }

  ok = bolt_read_int_at (dirfd (parent), "authorized", auth, &err);

  if (!ok)
    {
      bolt_warn_err (err, LOG_DEV (dev), LOG_TOPIC ("authorize"),
                     "could not read parent authorization");
      return FALSE;
    }

  return TRUE;
}

static void
authorize_adjust_error (BoltDevice *dev,
                        DIR        *devdir,
                        GError    **error)
{
  GError *err;
  gint auth = -1;
  gboolean ok;

  if (error == NULL)
    return;

  err = *error;

  if (bolt_err_inval (err))
    {
      /* EINVAL is reported by the kernel if:
       *  a) device is already authorized
       *  b) parent device is *not* authorized
       */

      /* check for a) */
      ok = bolt_read_int_at (dirfd (devdir), "authorized", &auth, NULL);
      if (ok && auth > 0)
        {
          g_clear_error (error);
          g_set_error_literal (error, BOLT_ERROR, BOLT_ERROR_BADSTATE,
                               "device is already authorized");

          return;
        }

      /* check for b) */
      ok = device_check_parent_auth (dev, devdir, &auth);
      if (ok && auth < 1)
        {
          /* parent is not authorized, adjust the error */
          g_clear_error (error);
          g_set_error_literal (error, BOLT_ERROR, BOLT_ERROR_AUTHCHAIN,
                               "parent device is not authorized");

          return;
        }
    }

  /* if we have a generic, non bolt error, it is most likely a
   * G_IO_ERROR. We prefix the error message to make it clearer
   * where the (probably cryptic) error originated
   */
  if (err->domain != BOLT_ERROR)
    g_prefix_error (error, "%s", "kernel error: ");
}

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

      bolt_debug (LOG_DEV (dev), LOG_TOPIC ("authorize"), "writing key");
      keyfd = bolt_openat (dirfd (devdir), "key", O_WRONLY | O_CLOEXEC, error);
      if (keyfd < 0)
        return FALSE;

      ok = bolt_key_write_to (key, keyfd, &level, error);
      close (keyfd);
      if (!ok)
        return FALSE;
    }

  bolt_debug (LOG_DEV (dev), LOG_TOPIC ("authorize"),
              "writing authorization");

  ok = bolt_write_char_at (dirfd (devdir),
                           "authorized",
                           level,
                           error);

  if (!ok)
    authorize_adjust_error (dev, devdir, error);

  return ok;
}

static void
authorize_in_thread (GTask        *task,
                     gpointer      source,
                     gpointer      context,
                     GCancellable *cancellable)
{
  GError *error = NULL;
  BoltDevice *dev = source;
  AuthData *auth_data = context;
  BoltAuth *auth = auth_data->auth;
  gboolean ok;

  ok = authorize_device_internal (dev, auth, &error);

  if (!ok)
    g_task_return_error (task, error);
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
  guint aflags;
  guint mask;
  gboolean chg;
  gboolean ok;
  guint64 now;

  auth_data = g_task_get_task_data (task);
  auth = auth_data->auth;

  ok = g_task_propagate_boolean (task, &error);

  if (!ok)
    bolt_auth_return_error (auth, &error);

  now = bolt_now_in_seconds ();
  status = bolt_auth_to_status (auth);
  aflags = bolt_auth_to_flags (auth, &mask);

  bolt_info (LOG_DEV (dev), LOG_TOPIC ("authorize"),
             "finished: %s (status: %s, flags: %u)",
             ok ? "ok" : "FAIL",
             bolt_status_to_string (status),
             aflags);

  g_object_freeze_notify (object);

  dev->authtime = now;
  g_object_notify_by_pspec (G_OBJECT (dev), props[PROP_AUTHTIME]);

  chg = bolt_flags_update (aflags, &dev->aflags, mask);
  if (chg)
    g_object_notify_by_pspec (object, props[PROP_AUTHFLAGS]);

  device_set_status_internal (dev, status, TRUE);

  g_object_thaw_notify (object);

  if (auth_data->callback)
    auth_data->callback (G_OBJECT (dev),
                         G_ASYNC_RESULT (auth),
                         auth_data->user_data);
}

static GTask *
authorize_prepare (BoltDevice         *dev,
                   BoltAuth           *auth,
                   GAsyncReadyCallback callback,
                   gpointer            user_data)
{
  BoltSecurity lvl;
  AuthData *auth_data;
  GTask *task;

  g_object_set (auth, "device", dev, NULL);

  if (!bolt_status_is_pending (dev->status))
    {
      bolt_auth_return_new_error (auth, BOLT_ERROR, BOLT_ERROR_BADSTATE,
                                  "wrong device state: %s",
                                  bolt_status_to_string (dev->status));

      if (callback)
        callback (G_OBJECT (dev), G_ASYNC_RESULT (auth), user_data);

      return NULL;
    }

  task = g_task_new (dev, NULL, authorize_thread_done, NULL);
  auth_data = g_slice_new (AuthData);
  auth_data->callback = callback;
  auth_data->user_data = user_data;
  auth_data->auth = g_object_ref (auth);
  g_task_set_task_data (task, auth_data, auth_data_free);

  g_object_set (dev, "status", BOLT_STATUS_AUTHORIZING, NULL);

  lvl = bolt_auth_get_level (auth);
  bolt_info (LOG_DEV (dev), LOG_TOPIC ("authorize"),
             "authorization prepared for '%s' level",
             bolt_security_to_string (lvl));

  return task;
}

static gboolean
authorize_device_idle (gpointer user_data)
{
  g_autoptr(GTask) task = NULL;

  g_return_val_if_fail (G_IS_TASK (user_data), G_SOURCE_REMOVE);
  task = (GTask *) user_data;

  g_task_run_in_thread (task, authorize_in_thread);

  return G_SOURCE_REMOVE;
}

/* dbus property setter */

static gboolean
handle_set_label (BoltExported *obj,
                  const char   *name,
                  const GValue *value,
                  GError      **error)
{
  g_autofree char *nick = NULL;
  g_autofree char *old = NULL;
  BoltDevice *dev = BOLT_DEVICE (obj);
  const char *str = g_value_get_string (value);
  gboolean ok;

  nick = bolt_strdup_validate (str);

  if (nick == NULL)
    {
      g_set_error_literal (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                           "string is invalid");
      return FALSE;
    }
  else if (strlen (nick) > 255)
    {
      g_set_error_literal (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                           "string is too long");
      return FALSE;
    }

  if (dev->store == NULL)
    {
      g_set_error_literal (error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                           "device is not stored");
      return FALSE;
    }

  old = dev->label;
  dev->label = g_steal_pointer (&nick);

  ok = bolt_store_put_device (dev->store, dev, dev->policy, NULL, error);

  if (!ok)
    {
      bolt_warn_err (*error, LOG_DEV (dev), "failed to store device");

      nick = dev->label;
      dev->label = g_steal_pointer (&old);
    }

  return ok;
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
  gboolean ok;

  inv  = user_data;
  auth = BOLT_AUTH (res);

  ok = bolt_auth_check (auth, &error);
  if (ok)
    g_dbus_method_invocation_return_value (inv, g_variant_new ("()"));
  else
    g_dbus_method_invocation_take_error (inv, error);
}

static GVariant *
handle_authorize (BoltExported          *object,
                  GVariant              *params,
                  GDBusMethodInvocation *inv,
                  GError               **error)
{
  g_autoptr(BoltAuth) auth = NULL;
  BoltDevice *dev = BOLT_DEVICE (object);
  BoltSecurity level;
  BoltKey *key;

  level = bolt_domain_get_security (dev->domain);
  key = NULL;

  if (level == BOLT_SECURITY_SECURE)
    {
      if (dev->key)
        key = bolt_store_get_key (dev->store, dev->uid, error);
      else
        level = BOLT_SECURITY_USER;
    }

  /* only happens if the key could not be read */
  if (level == BOLT_SECURITY_SECURE && key == NULL)
    return NULL;

  auth = bolt_auth_new (dev, level, key);
  bolt_device_authorize (dev, auth, handle_authorize_done, inv);

  return NULL;
}

/* public methods */

BoltDevice *
bolt_device_new_for_udev (struct udev_device *udev,
                          BoltDomain         *domain,
                          GError            **error)
{
  BoltDevInfo info;
  const char *uid;
  const char *name;
  const char *vendor;
  BoltStatus status;
  BoltAuthFlags aflags;
  BoltDeviceType type;
  BoltDevice *dev;
  gboolean ok;
  guint64 ct, at;

  g_return_val_if_fail (udev != NULL, NULL);
  g_return_val_if_fail (domain != NULL, NULL);

  uid = udev_device_get_sysattr_value (udev, "unique_id");
  if (uid == NULL)
    {
      g_set_error (error, BOLT_ERROR, BOLT_ERROR_UDEV,
                   "could not get unique_id for udev");
      return NULL;
    }

  name = read_sysattr_name (udev, "device", error);
  if (name == NULL)
    return NULL;

  vendor = read_sysattr_name (udev, "vendor", error);
  if (vendor == NULL)
    return NULL;

  ok = bolt_sysfs_info_for_device (udev, TRUE, &info, error);
  if (!ok)
    return NULL;

  if (info.parent == NULL)
    type = BOLT_DEVICE_HOST;
  else
    type = BOLT_DEVICE_PERIPHERAL;

  ct = (guint64) info.ctim;

  status = bolt_status_from_info (&info);
  aflags = bolt_auth_flags_from_info (&info, domain, NULL);
  at = bolt_status_is_authorized (status) ? ct : 0;

  dev = g_object_new (BOLT_TYPE_DEVICE,
                      "uid", uid,
                      "name", name,
                      "vendor", vendor,
                      "type", type,
                      "status", status,
                      "authflags", aflags,
                      "sysfs-path", info.syspath,
                      "domain", domain,
                      "parent", info.parent,
                      "conntime", ct,
                      "authtime", at,
                      NULL);

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

  ok = bolt_exported_export (BOLT_EXPORTED (device),
                             connection,
                             path,
                             error);

  return ok ? path : NULL;
}

void
bolt_device_unexport (BoltDevice *device)
{
  bolt_exported_unexport (BOLT_EXPORTED (device));
}

void
bolt_device_authorize (BoltDevice         *dev,
                       BoltAuth           *auth,
                       GAsyncReadyCallback callback,
                       gpointer            user_data)
{
  g_autoptr(GTask) task = NULL;

  task = authorize_prepare (dev, auth, callback, user_data);

  if (task == NULL)
    return;

  g_task_run_in_thread (task, authorize_in_thread);
}

void
bolt_device_authorize_idle (BoltDevice         *dev,
                            BoltAuth           *auth,
                            GAsyncReadyCallback callback,
                            gpointer            user_data)
{
  GTask *task = NULL;

  task = authorize_prepare (dev, auth, callback, user_data);

  if (task == NULL)
    return;

  g_idle_add (authorize_device_idle, task);
}

BoltStatus
bolt_device_connected (BoltDevice         *dev,
                       BoltDomain         *domain,
                       struct udev_device *udev)
{
  g_autoptr(GError) err = NULL;
  BoltAuthFlags aflags;
  BoltDevInfo info;
  BoltStatus status;
  gboolean ok;
  guint64 ct, at;

  g_return_val_if_fail (dev != NULL, BOLT_STATUS_UNKNOWN);
  g_return_val_if_fail (domain != NULL, BOLT_STATUS_UNKNOWN);

  ok = bolt_sysfs_info_for_device (udev, TRUE, &info, &err);
  if (!ok)
    bolt_warn_err (err, LOG_DEV (dev), LOG_TOPIC ("udev"),
                   "failed to get device info");

  status = bolt_status_from_info (&info);
  aflags = bolt_auth_flags_from_info (&info, domain, NULL);

  ct = (guint64) info.ctim;
  at = bolt_status_is_authorized (status) ? ct : 0;

  g_object_set (G_OBJECT (dev),
                "parent", info.parent,
                "sysfs-path", info.syspath,
                "domain", domain,
                "status", status,
                "authflags", aflags,
                "conntime", ct,
                "authtime", at,
                NULL);

  bolt_info (LOG_DEV (dev), "parent is %.13s...", dev->parent);

  return status;
}

BoltStatus
bolt_device_disconnected (BoltDevice *dev)
{
  g_object_set (G_OBJECT (dev),
                "parent", NULL,
                "sysfs-path", NULL,
                "domain", NULL,
                "status", BOLT_STATUS_DISCONNECTED,
                "conntime", 0,
                "authtime", 0,
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
bolt_device_is_connected (const BoltDevice *device)
{
  return bolt_status_is_connected (device->status);
}

gboolean
bolt_device_is_authorized (const BoltDevice *device)
{
  return bolt_status_is_authorized (device->status);
}

BoltStatus
bolt_device_update_from_udev (BoltDevice         *dev,
                              struct udev_device *udev)
{
  g_autoptr(GError) err = NULL;
  BoltAuthFlags aflags;
  BoltDevInfo info;
  BoltStatus status;
  guint mask;
  gboolean chg;
  gboolean ok;

  /* if we are currently authorizing, let's not update
   * the status, because we are most likely causing that
   * udev update and we cannot determine AUTHORIZING from
   * outside;
   * The status will be set by authorize_thread_done()
   */
  if (dev->status == BOLT_STATUS_AUTHORIZING)
    return dev->status;

  ok = bolt_sysfs_info_for_device (udev, FALSE, &info, &err);

  if (!ok)
    {
      bolt_warn_err (err, LOG_DEV (dev), LOG_TOPIC ("udev"),
                     "failed to get device info");
      return dev->status;
    }

  status = bolt_status_from_info (&info);
  aflags = bolt_auth_flags_from_info (&info, dev->domain, &mask);

  g_object_freeze_notify (G_OBJECT (dev));

  if (bolt_status_is_authorized (status) &&
      !bolt_status_is_authorized (dev->status))
    {
      dev->authtime = bolt_now_in_seconds ();
      g_object_notify_by_pspec (G_OBJECT (dev), props[PROP_AUTHTIME]);
    }

  chg = bolt_flags_update (aflags, &dev->aflags, mask);
  if (chg)
    g_object_notify_by_pspec (G_OBJECT (dev), props[PROP_AUTHFLAGS]);

  device_set_status_internal (dev, status, TRUE);

  g_object_thaw_notify (G_OBJECT (dev));

  return status;
}

BoltKeyState
bolt_device_get_keystate (const BoltDevice *dev)
{
  return dev->key;
}

const char *
bolt_device_get_name (const BoltDevice *dev)
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
bolt_device_get_policy (const BoltDevice *dev)
{
  return dev->policy;
}

const char *
bolt_device_get_uid (const BoltDevice *dev)
{
  return dev->uid;
}

BoltSecurity
bolt_device_get_security (const BoltDevice *dev)
{
  g_return_val_if_fail (dev != NULL, BOLT_SECURITY_UNKNOWN);
  g_return_val_if_fail (dev->domain != NULL, BOLT_SECURITY_UNKNOWN);

  return bolt_domain_get_security (dev->domain);
}

BoltStatus
bolt_device_get_status (const BoltDevice *dev)
{
  return dev->status;
}

BoltAuthFlags
bolt_device_get_authflags (const BoltDevice *dev)
{
  return dev->aflags;
}

gboolean
bolt_device_get_stored (const BoltDevice *dev)
{
  return dev->store != NULL;
}

const char *
bolt_device_get_syspath (const BoltDevice *dev)
{
  return dev->syspath;
}

const char *
bolt_device_get_vendor (const BoltDevice *dev)
{
  return dev->vendor;
}

BoltDeviceType
bolt_device_get_device_type (const BoltDevice *dev)
{
  return dev->type;
}

const char *
bolt_device_get_label (const BoltDevice *dev)
{
  return dev->label;
}

gint64
bolt_device_get_storetime (const BoltDevice *dev)
{
  return dev->storetime;
}

gboolean
bolt_device_supports_secure_mode (const BoltDevice *dev)
{
  return bolt_flag_isclear (dev->aflags, BOLT_AUTH_NOKEY);
}

gboolean
bolt_device_check_authflag (const BoltDevice *dev,
                            BoltAuthFlags     flag)
{
  return bolt_flag_isset (dev->aflags, flag);
}

BoltKey *
bolt_device_get_key_from_sysfs (const BoltDevice *dev,
                                GError          **error)
{
  g_autoptr(GFile) keyfile = NULL;
  g_autofree char *keypath = NULL;
  BoltKey *key;

  g_return_val_if_fail (dev != NULL, NULL);

  if (dev->syspath == NULL)
    return NULL;

  keypath = g_build_filename (dev->syspath, "key", NULL);
  keyfile = g_file_new_for_path (keypath);

  return key = bolt_key_load_file (keyfile, error);
}

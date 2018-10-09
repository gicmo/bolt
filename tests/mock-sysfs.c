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

#include "bolt-io.h"
#include "bolt-str.h"

#include "mock-sysfs.h"

#include <umockdev.h>

#include <glib/gstdio.h>

#include <errno.h>
#include <sys/stat.h>

typedef struct _MockDevice MockDevice;
struct _MockDevice
{
  char *idstr;
  char *path;

  gint  serial;

  /*  */
  GHashTable *devices;
};

static void
mock_device_destroy (gpointer data)
{
  MockDevice *device = data;

  if (device == NULL)
    return;

  g_clear_pointer (&device->devices, g_hash_table_unref);
  g_free (device->path);
  g_free (device);
}

typedef struct _MockDomain MockDomain;

struct _MockDomain
{
  guint       id;

  char       *idstr;
  char       *path;

  gint        serial;

  MockDevice *host;
};

static void
mock_domain_destory (gpointer data)
{
  MockDomain *domain = data;

  mock_device_destroy (domain->host);

  g_free (domain->path);
  g_free (domain);
}

/* prototypes */

static MockDevice *    mock_sysfs_device_plug (MockSysfs  *ms,
                                               MockDomain *domain,
                                               char       *parent,
                                               MockDevId  *id,
                                               guint       authorized,
                                               const char *key,
                                               gint        boot);

static void            mock_sysfs_device_unplug (MockSysfs  *ms,
                                                 MockDevice *dev);


struct _MockSysfs
{
  GObject object;

  /* umockdev */
  UMockdevTestbed *bed;

  /* state tracking */
  char       *force_power;
  GHashTable *domains;
  GHashTable *devices;
};


enum {
  PROP_0,

  PROP_TESTBED,

  PROP_LAST
};

static GParamSpec *sysfs_props[PROP_LAST] = { NULL, };

G_DEFINE_TYPE (MockSysfs,
               mock_sysfs,
               G_TYPE_OBJECT);


static void
mock_sysfs_finalize (GObject *object)
{
  MockSysfs *ms = MOCK_SYSFS (object);

  g_clear_object (&ms->bed);
  g_clear_pointer (&ms->domains, g_hash_table_unref);
  g_clear_pointer (&ms->devices, g_hash_table_unref);

  G_OBJECT_CLASS (mock_sysfs_parent_class)->finalize (object);
}

static void
mock_sysfs_init (MockSysfs *ms)
{
  g_autofree char *bus = NULL;
  g_autofree char *cls = NULL;
  g_autofree char *sys = NULL;
  int r;

  ms->bed = umockdev_testbed_new ();
  ms->domains = g_hash_table_new_full (g_str_hash, g_str_equal,
                                       NULL, mock_domain_destory);

  ms->devices = g_hash_table_new (g_str_hash, g_str_equal);

  /* udev_enumerate_scan_devices() will return -ENOENT, if
   * sys/bus or sys/class directories can not be found
   */
  sys = umockdev_testbed_get_sys_dir (ms->bed);

  bus = g_build_filename (sys, "bus", NULL);
  r = g_mkdir (bus, 0744);

  if (r < 0)
    g_warning ("could not create %s", bus);

  cls = g_build_filename (sys, "class", NULL);
  r = g_mkdir (cls, 0744);
  if (r < 0)
    g_warning ("could not create %s", bus);
}

static void
mock_sysfs_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  MockSysfs *ms = MOCK_SYSFS (object);

  switch (prop_id)
    {
    case PROP_TESTBED:
      g_value_set_object (value, ms->bed);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
mock_sysfs_class_init (MockSysfsClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = mock_sysfs_finalize;

  gobject_class->get_property = mock_sysfs_get_property;

  sysfs_props[PROP_TESTBED] =
    g_param_spec_object ("testbed",
                         NULL, NULL,
                         UMOCKDEV_TYPE_TESTBED,
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_NICK);

  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     sysfs_props);
}

/* internal */
#define CONST_STRV(...) (char **) (const char *[]){ __VA_ARGS__}

static MockDevice *
mock_sysfs_device_plug (MockSysfs  *ms,
                        MockDomain *domain,
                        char       *parent,
                        MockDevId  *id,
                        guint       authorized,
                        const char *key,
                        gint        boot)
{
  g_autofree char *idstr = NULL;
  g_autofree char *vendor_id = NULL;
  g_autofree char *device_id = NULL;
  g_autofree char *authstr = NULL;
  g_autofree char *bootstr = NULL;
  const char *props[17] = {NULL, };
  MockDevice *device;
  guint serial;
  char *path;
  guint i;

  serial = (domain->serial)++;
  idstr = g_strdup_printf ("%u-%u", domain->id, serial);

  vendor_id = g_strdup_printf ("%d", id->vendor_id);
  device_id = g_strdup_printf ("%d", id->device_id);
  authstr = g_strdup_printf ("%u", authorized);

  i = 0;
  props[i++] = "vendor";
  props[i++] = vendor_id;
  props[i++] = "vendor_name";
  props[i++] = id->vendor_name;
  props[i++] = "device";
  props[i++] = device_id;
  props[i++] = "device_name";
  props[i++] = id->device_name;
  props[i++] = "unique_id";
  props[i++] = id->unique_id;
  props[i++] = "authorized";
  props[i++] = authstr;

  if (key != NULL)
    {
      props[i++] = "key";
      props[i++] = key;
    }

  if (boot > 0)
    {
      bootstr = g_strdup_printf ("%d", boot);
      props[i++] = "boot";
      props[i++] = bootstr;
    }

  props[i++] = NULL;
  g_assert (sizeof (props) >= i);


  path = umockdev_testbed_add_devicev (ms->bed, "thunderbolt", idstr,
                                       parent,
                                       (char **) props,
                                       CONST_STRV ("DEVTYPE", "thunderbolt_device", NULL));

  if (path == NULL)
    return NULL;

  g_debug ("M [A] %s (%s) @ %s", idstr, authstr, path);

  device = g_new0 (MockDevice, 1);
  device->idstr = g_steal_pointer (&idstr);
  device->path = path;
  device->devices = g_hash_table_new_full (g_str_hash, g_str_equal,
                                           NULL, mock_device_destroy);

  g_hash_table_replace (ms->devices, device->idstr, device);

  return device;
}

static void
mock_sysfs_device_unplug (MockSysfs  *ms,
                          MockDevice *dev)
{

  GHashTableIter iter;
  gpointer k, v;

  g_hash_table_iter_init (&iter, dev->devices);
  while (g_hash_table_iter_next (&iter, &k, &v))
    {
      MockDevice *child = v;
      mock_sysfs_device_unplug (ms, child);

      g_hash_table_iter_remove (&iter);
    }

  g_clear_pointer (&dev->devices, g_hash_table_unref);

  g_debug ("M [R] %s @ %s", dev->idstr, dev->path);

  umockdev_testbed_uevent (ms->bed, dev->path, "remove");
  umockdev_testbed_remove_device (ms->bed, dev->path);
}

/* public methods: generic */

MockSysfs *
mock_sysfs_new (void)
{
  MockSysfs *ms;

  ms = g_object_new (MOCK_TYPE_SYSFS, NULL);

  return ms;
}


/* public methods: force-power */
const char *
mock_sysfs_force_power_add (MockSysfs *ms)
{
  char *path;

  g_return_val_if_fail (MOCK_IS_SYSFS (ms), NULL);
  g_return_val_if_fail (ms->force_power == NULL, NULL);

  path = umockdev_testbed_add_device (ms->bed, "wmi", INTEL_WMI_THUNDERBOLT_GUID,
                                      NULL,
                                      "force_power", "",
                                      NULL,
                                      "WMI_GUID", INTEL_WMI_THUNDERBOLT_GUID,
                                      "DRIVER", "intel-wmi-thunderbolt",
                                      NULL);


  ms->force_power = path;

  return path;
}

gboolean
mock_sysfs_force_power_remove (MockSysfs *ms)
{

  g_return_val_if_fail (MOCK_IS_SYSFS (ms), FALSE);
  g_return_val_if_fail (ms->force_power != NULL, FALSE);

  umockdev_testbed_uevent (ms->bed, ms->force_power, "remove");
  umockdev_testbed_remove_device (ms->bed, ms->force_power);

  return TRUE;
}

void
mock_sysfs_force_power_load (MockSysfs *ms)
{
  g_return_if_fail (MOCK_IS_SYSFS (ms));
  g_return_if_fail (ms->force_power != NULL);

  umockdev_testbed_set_attribute (ms->bed, ms->force_power,
                                  "force_power", "");
  umockdev_testbed_set_property (ms->bed, ms->force_power,
                                 "DRIVER", "intel-wmi-thunderbolt");
  umockdev_testbed_uevent (ms->bed, ms->force_power, "change");
  umockdev_testbed_uevent (ms->bed, ms->force_power, "bind");
}

void
mock_sysfs_force_power_unload (MockSysfs *ms)
{
  g_autofree char *root = NULL;
  g_autofree char *path = NULL;
  int r;

  g_return_if_fail (MOCK_IS_SYSFS (ms));
  g_return_if_fail (ms->force_power != NULL);

  root = umockdev_testbed_get_root_dir (ms->bed);
  path = g_build_filename (root, ms->force_power, "force_power", NULL);
  r = g_unlink (path);

  if (r == -1)
    g_warning ("could not unlink %s: %s", path, g_strerror (errno));

  umockdev_testbed_set_property (ms->bed, ms->force_power, "DRIVER", "");
  umockdev_testbed_uevent (ms->bed, ms->force_power, "change");
  umockdev_testbed_uevent (ms->bed, ms->force_power, "unbind");
}

char *
mock_sysfs_force_power_read (MockSysfs *ms)
{
  g_autoptr(GError) err = NULL;
  g_autofree char *path = NULL;
  char *data;
  gboolean ok;

  g_return_val_if_fail (MOCK_IS_SYSFS (ms), NULL);
  g_return_val_if_fail (ms->force_power != NULL, NULL);

  path = g_build_filename (ms->force_power, "force_power", NULL);
  ok = g_file_get_contents (path, &data, NULL, &err);

  if (!ok)
    {
      g_warning ("could not read force power file: %s",
                 err->message);
      return NULL;
    }

  return data;
}

gboolean
mock_sysfs_force_power_enabled (MockSysfs *ms)
{
  g_autofree char *data = NULL;

  data = mock_sysfs_force_power_read (ms);

  data = g_strstrip (data);
  return bolt_streq (data, "1");
}

/* public methods: domain */

const char *
mock_sysfs_domain_add (MockSysfs   *ms,
                       BoltSecurity security,
                       GStrv        bootacl)
{
  g_autofree char *acl = NULL;
  const char *props[5] = {NULL, };
  const char *secstr;
  MockDomain *domain;
  char *idstr = NULL;
  char *path;
  guint id;
  guint i;

  g_return_val_if_fail (MOCK_IS_SYSFS (ms), NULL);

  id = g_hash_table_size (ms->domains);

  secstr = bolt_security_to_string (security);
  idstr = g_strdup_printf ("domain%u", id);

  i = 0;
  props[i++] = "security";
  props[i++] = secstr;
  if (bootacl != NULL)
    {
      acl = g_strjoinv (",", bootacl);
      props[i++] = "boot_acl";
      props[i++] = acl;
    }
  props[i++] = NULL;

  g_assert (sizeof (props) >= i);

  path = umockdev_testbed_add_devicev (ms->bed, "thunderbolt", idstr,
                                       NULL, /* parent: domain has none */
                                       (char **) props,
                                       CONST_STRV ("DEVTYPE", "thunderbolt_domain", NULL));

  if (path == NULL)
    return path;

  g_debug ("M [A] %s (%s) @ %s", idstr, secstr, path);

  domain = g_new0 (MockDomain, 1);

  domain->id = 0;
  domain->idstr = idstr;
  domain->path = path;

  g_hash_table_insert (ms->domains, idstr, domain);

  return idstr;
}

const char *
mock_sysfs_domain_get_syspath (MockSysfs  *ms,
                               const char *id)
{
  MockDomain *domain;

  g_return_val_if_fail (MOCK_IS_SYSFS (ms), NULL);
  g_return_val_if_fail (id != NULL, NULL);

  domain = g_hash_table_lookup (ms->domains, id);

  if (domain == NULL)
    return NULL;

  return domain->path;
}

gboolean
mock_sysfs_domain_remove (MockSysfs  *ms,
                          const char *id)
{
  MockDomain *domain;

  g_return_val_if_fail (MOCK_IS_SYSFS (ms), FALSE);
  g_return_val_if_fail (id != NULL, FALSE);

  domain = g_hash_table_lookup (ms->domains, id);

  if (domain == NULL)
    return FALSE;

  if (domain->host)
    {
      mock_sysfs_device_unplug (ms, domain->host);
      g_clear_pointer (&domain->host, mock_device_destroy);
    }

  g_debug ("M [R] %s @ %s", domain->idstr, domain->path);

  umockdev_testbed_uevent (ms->bed, domain->path, "remove");
  umockdev_testbed_remove_device (ms->bed, domain->path);

  g_hash_table_remove (ms->domains, id);
  return TRUE;
}

GStrv
mock_sysfs_domain_bootacl_get (MockSysfs  *ms,
                               const char *id,
                               GError    **error)
{
  g_autofree char *path = NULL;
  g_autofree char *data = NULL;
  GStrv acl;
  MockDomain *domain;
  gboolean ok;

  g_return_val_if_fail (MOCK_IS_SYSFS (ms), NULL);
  g_return_val_if_fail (id != NULL, NULL);

  domain = g_hash_table_lookup (ms->domains, id);

  if (domain == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "domain '%s' not found", id);
      return NULL;
    }

  path = g_build_filename (domain->path, "boot_acl", NULL);

  ok = g_file_get_contents (path, &data, NULL, error);
  if (!ok)
    return NULL;

  acl = g_strsplit (data, ",", -1);
  return acl;
}

gboolean
mock_sysfs_domain_bootacl_set (MockSysfs  *ms,
                               const char *id,
                               GStrv       acl,
                               GError    **error)
{
  g_autofree char *path = NULL;
  g_autofree char *data = NULL;
  MockDomain *domain;
  gboolean ok;

  g_return_val_if_fail (MOCK_IS_SYSFS (ms), FALSE);
  g_return_val_if_fail (id != NULL, FALSE);
  g_return_val_if_fail (acl != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  domain = g_hash_table_lookup (ms->domains, id);

  if (domain == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "domain '%s' not found", id);
      return FALSE;
    }

  data = g_strjoinv (",", acl);
  path = g_build_filename (domain->path, "boot_acl", NULL);

  ok = bolt_file_write_all (path, data, -1, error);
  if (!ok)
    return FALSE;

  umockdev_testbed_uevent (ms->bed, domain->path, "change");
  return TRUE;
}

const char *
mock_sysfs_host_add (MockSysfs  *ms,
                     const char *dom,
                     MockDevId  *id)
{
  MockDomain *domain;
  MockDevice *device;

  g_return_val_if_fail (MOCK_IS_SYSFS (ms), NULL);
  g_return_val_if_fail (dom != NULL, NULL);

  domain = g_hash_table_lookup (ms->domains, dom);

  if (domain == NULL)
    {
      g_warning ("domain '%s' not found", dom);
      return NULL;
    }

  if (domain->host != NULL)
    {
      g_warning ("domain '%s' already has a host", dom);
      return NULL;
    }

  device = mock_sysfs_device_plug (ms,
                                   domain,
                                   domain->path,
                                   id,
                                   1,
                                   NULL, /* no key for the host */
                                   -1);  /* no boot file either */
  domain->host = device;

  return device->idstr;
}

const char *
mock_sysfs_device_add (MockSysfs  *ms,
                       const char *parent,
                       MockDevId  *id,
                       guint       authorized,
                       const char *key,
                       gint        boot)
{
  MockDevice *pdev;
  MockDomain *domain = NULL;
  MockDevice *device;
  GHashTableIter iter;
  gpointer k, v;

  g_return_val_if_fail (MOCK_IS_SYSFS (ms), NULL);
  g_return_val_if_fail (parent != NULL, NULL);

  pdev = g_hash_table_lookup (ms->devices, parent);
  if (pdev == NULL)
    {
      g_warning ("parent device '%s' not found", parent);
      return NULL;
    }

  /* look up the domain for the device */
  g_hash_table_iter_init (&iter, ms->domains);
  while (g_hash_table_iter_next (&iter, &k, &v))
    {
      MockDomain *d = v;

      if (g_str_has_prefix (pdev->path, d->path))
        {
          domain = d;
          break;
        }
    }

  if (domain == NULL)
    {
      g_warning ("domain not found for device '%s'", parent);
      return NULL;
    }

  device = mock_sysfs_device_plug (ms,
                                   domain,
                                   pdev->path,
                                   id,
                                   1,
                                   key,
                                   boot);

  g_hash_table_insert (pdev->devices, device->idstr, device);

  return device->idstr;
}


const char *
mock_sysfs_device_get_syspath (MockSysfs  *ms,
                               const char *id)
{
  MockDevice *dev;

  g_return_val_if_fail (MOCK_IS_SYSFS (ms), NULL);
  g_return_val_if_fail (id != NULL, NULL);

  dev = g_hash_table_lookup (ms->devices, id);

  if (dev == NULL)
    return NULL;

  return dev->path;
}

const char *
mock_sysfs_device_get_parent (MockSysfs  *ms,
                              const char *id)
{
  MockDevice *dev;
  GHashTableIter iter;
  gpointer k, v;

  g_return_val_if_fail (MOCK_IS_SYSFS (ms), FALSE);
  g_return_val_if_fail (id != NULL, FALSE);

  dev = g_hash_table_lookup (ms->devices, id);

  if (dev == NULL)
    return FALSE;

  /* we look for the device that contains 'dev'
   * in its devices table, because that will be
   * the parent */
  g_hash_table_iter_init (&iter, ms->devices);
  while (g_hash_table_iter_next (&iter, &k, &v))
    {
      MockDevice *d = v;
      gpointer p;

      p = g_hash_table_lookup (d->devices, dev->path);
      if (p != NULL)
        return d->idstr;
    }

  return NULL;
}

gboolean
mock_sysfs_device_remove (MockSysfs  *ms,
                          const char *id)
{
  const char *mom;
  MockDevice *m;
  MockDevice *dev;


  g_return_val_if_fail (MOCK_IS_SYSFS (ms), FALSE);
  g_return_val_if_fail (id != NULL, FALSE);

  dev = g_hash_table_lookup (ms->devices, id);

  if (dev == NULL)
    return FALSE;

  mom = mock_sysfs_device_get_parent (ms, id);
  if (mom == NULL)
    return FALSE;

  m =  g_hash_table_lookup (ms->devices, mom);
  g_assert_nonnull (m);

  mock_sysfs_device_unplug (ms, dev);
  g_hash_table_remove (m->devices, id);
  mock_device_destroy (dev);

  return TRUE;
}

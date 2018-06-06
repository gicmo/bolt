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

#include "bolt-str.h"

#include "mock-sysfs.h"

#include <umockdev.h>

#include <glib/gstdio.h>
#include <sys/stat.h>

typedef struct _MockDomain MockDomain;

struct _MockDomain
{
  guint id;

  char *idstr;
  char *path;

  gint  devices;
};

static void
mock_domain_destory (gpointer data)
{
  MockDomain *domain = data;

  g_free (domain->path);
  g_free (domain);
}

struct _MockSysfs
{
  GObject object;

  /* umockdev */
  UMockdevTestbed *bed;

  /* state tracking */
  GHashTable *domains;
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

MockSysfs *
mock_sysfs_new (void)
{
  MockSysfs *ms;

  ms = g_object_new (MOCK_TYPE_SYSFS, NULL);

  return ms;
}

const char *
mock_sysfs_domain_add (MockSysfs   *ms,
                       BoltSecurity security)
{
  const char *secstr;
  MockDomain *domain;
  char *idstr = NULL;
  char *path;
  guint id;

  g_return_val_if_fail (MOCK_IS_SYSFS (ms), NULL);

  id = g_hash_table_size (ms->domains);

  secstr = bolt_security_to_string (security);
  idstr = g_strdup_printf ("domain%u", id);

  path = umockdev_testbed_add_device (ms->bed, "thunderbolt", idstr,
                                      NULL,
                                      "security", secstr,
                                      NULL,
                                      "DEVTYPE", "thunderbolt_domain",
                                      NULL);

  if (path == NULL)
    return path;

  domain = g_new0 (MockDomain, 1);

  domain->id = 0;
  domain->idstr = path;
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

  return domain->idstr;
}

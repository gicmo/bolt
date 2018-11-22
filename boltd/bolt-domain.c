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

#include "bolt-error.h"
#include "bolt-log.h"
#include "bolt-str.h"
#include "bolt-store.h"
#include "bolt-sysfs.h"

#include "bolt-list.h"
#include "bolt-domain.h"

#include <gio/gio.h>

#include <libudev.h>

static void bolt_domain_bootacl_open_log (BoltDomain *domain);

struct _BoltDomain
{
  BoltExported object;

  /* internal list  */
  BoltList     domains;
  gint         sort;

  BoltStore   *store;
  BoltJournal *acllog;

  /* persistent */
  char *uid;

  /* sysfs */
  char        *id;
  char        *syspath;
  BoltSecurity security;
  GStrv        bootacl;
};


enum {
  PROP_0,

  /* internal properties */
  PROP_STORE,
  PROP_OBJECT_ID,

  /* exported properties */
  PROP_UID,
  PROP_ID,
  PROP_SYSPATH,
  PROP_SECURITY,
  PROP_BOOTACL,

  PROP_LAST,
  PROP_EXPORTED = PROP_UID
};

static GParamSpec *props[PROP_LAST] = { NULL, };

enum {
  SIGNAL_BOOTACL_CHANGED,
  SIGNAL_BOOTACL_ALLOC,
  SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = {0, };

G_DEFINE_TYPE (BoltDomain,
               bolt_domain,
               BOLT_TYPE_EXPORTED)

static void
bolt_domain_finalize (GObject *object)
{
  BoltDomain *dom = BOLT_DOMAIN (object);

  g_clear_object (&dom->store);
  g_clear_object (&dom->acllog);

  g_free (dom->uid);
  g_free (dom->id);
  g_free (dom->syspath);
  g_strfreev (dom->bootacl);

  G_OBJECT_CLASS (bolt_domain_parent_class)->finalize (object);
}

static void
bolt_domain_init (BoltDomain *dom)
{
  bolt_list_init (&dom->domains);
}

static void
bolt_domain_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  BoltDomain *dom = BOLT_DOMAIN (object);

  switch (prop_id)
    {
    case PROP_STORE:
      g_value_set_object (value, dom->store);
      break;

    case PROP_OBJECT_ID:
    case PROP_UID:
      g_value_set_string (value, dom->uid);
      break;

    case PROP_ID:
      g_value_set_string (value, dom->id);
      break;

    case PROP_SYSPATH:
      g_value_set_string (value, dom->syspath);
      break;

    case PROP_SECURITY:
      g_value_set_enum (value, dom->security);
      break;

    case PROP_BOOTACL:
      g_value_set_boxed (value, dom->bootacl);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bolt_domain_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  BoltDomain *dom = BOLT_DOMAIN (object);

  switch (prop_id)
    {
    case PROP_STORE:
      if (dom->store == g_value_get_object (value))
        return;

      g_clear_object (&dom->store);
      dom->store = g_value_dup_object (value);

      if (dom->store)
        bolt_domain_bootacl_open_log (dom);
      else
        g_clear_object (&dom->acllog);

      break;

    case PROP_UID:
      dom->uid = g_value_dup_string (value);
      break;

    case PROP_ID:
      dom->id = g_value_dup_string (value);
      break;

    case PROP_SYSPATH:
      g_clear_pointer (&dom->syspath, g_free);
      dom->syspath = g_value_dup_string (value);
      break;

    case PROP_SECURITY:
      dom->security = g_value_get_enum (value);
      break;

    case PROP_BOOTACL:
      g_strfreev (dom->bootacl);
      dom->bootacl = g_value_dup_boxed (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bolt_domain_class_init (BoltDomainClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  BoltExportedClass *exported_class = BOLT_EXPORTED_CLASS (klass);

  gobject_class->finalize = bolt_domain_finalize;

  gobject_class->get_property = bolt_domain_get_property;
  gobject_class->set_property = bolt_domain_set_property;

  props[PROP_STORE] =
    g_param_spec_object ("store",
                         NULL, NULL,
                         BOLT_TYPE_STORE,
                         G_PARAM_READWRITE |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_OBJECT_ID] =
    bolt_param_spec_override (gobject_class, "object-id");

  props[PROP_UID] =
    g_param_spec_string ("uid",
                         "Uid", NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_ID] =
    g_param_spec_string ("id",
                         "Id", NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_SYSPATH] =
    g_param_spec_string ("syspath",
                         "SysfsPath", NULL,
                         NULL,
                         G_PARAM_READWRITE |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_SECURITY] =
    g_param_spec_enum ("security",
                       "SecurityLevel", NULL,
                       BOLT_TYPE_SECURITY,
                       BOLT_SECURITY_UNKNOWN,
                       G_PARAM_READWRITE |
                       G_PARAM_CONSTRUCT_ONLY |
                       G_PARAM_STATIC_STRINGS);

  props[PROP_BOOTACL] =
    g_param_spec_boxed ("bootacl",
                        "BootACL", NULL,
                        G_TYPE_STRV,
                        G_PARAM_READWRITE |
                        G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     props);

  bolt_exported_class_set_interface_info (exported_class,
                                          BOLT_DBUS_DOMAIN_INTERFACE,
                                          "/boltd/org.freedesktop.bolt.xml");

  bolt_exported_class_set_object_path (exported_class, BOLT_DBUS_PATH_DOMAINS);

  bolt_exported_class_export_properties (exported_class,
                                         PROP_EXPORTED,
                                         PROP_LAST,
                                         props);

  signals[SIGNAL_BOOTACL_CHANGED] =
    g_signal_new ("bootacl-changed",
                  BOLT_TYPE_DOMAIN,
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL,
                  NULL,
                  NULL,
                  G_TYPE_NONE,
                  2,
                  G_TYPE_BOOLEAN,
                  G_TYPE_HASH_TABLE);

  signals[SIGNAL_BOOTACL_ALLOC] =
    g_signal_new ("bootacl-alloc",
                  BOLT_TYPE_DOMAIN,
                  G_SIGNAL_RUN_LAST,
                  0,
                  g_signal_accumulator_true_handled,
                  NULL,
                  NULL,
                  G_TYPE_BOOLEAN,
                  3,
                  G_TYPE_STRV,
                  G_TYPE_STRING,
                  G_TYPE_POINTER);

}

/*  */
static void
bolt_domain_bootacl_open_log (BoltDomain *domain)
{
  g_autoptr(GError) err = NULL;
  BoltJournal *log = NULL;
  const char *uid = domain->uid;

  log = bolt_store_open_journal (domain->store,
                                 "bootacl",
                                 uid,
                                 &err);

  if (log == NULL)
    bolt_warn_err (err, LOG_TOPIC ("bootacl"), LOG_DOM (domain),
                   "could not open journal");

  domain->acllog = log;
}

static gboolean
bolt_domain_bootacl_can_update (BoltDomain *domain,
                                GError    **error)
{
  if (!bolt_domain_supports_bootacl (domain))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   "boot ACL not supported on domain '%s'", domain->uid);
      return FALSE;
    }

  if (domain->syspath == NULL && domain->acllog == NULL)
    {
      g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                           "domain offline and no bootacl journal");
      return FALSE;
    }

  return TRUE;
}

static void
bolt_domain_bootacl_update (BoltDomain *domain,
                            GStrv      *acl,
                            GHashTable *diff_hint)
{
  g_autoptr(GHashTable) diff = NULL;
  gboolean pending;
  gboolean same;
  guint signal;

  g_return_if_fail (domain != NULL);
  g_return_if_fail (acl != NULL);

  same = bolt_strv_equal (domain->bootacl, *acl);

  if (same)
    {
      bolt_debug (LOG_TOPIC ("bootacl"), LOG_DOM (domain),
                  "acl unchanged, not updating");
      return;
    }

  bolt_swap (domain->bootacl, *acl);

  g_object_notify_by_pspec (G_OBJECT (domain), props[PROP_BOOTACL]);

  if (domain->store)
    {
      g_autoptr(GError) err = NULL;
      gboolean ok;

      ok = bolt_store_put_domain (domain->store, domain, &err);

      if (!ok)
        bolt_warn_err (err, LOG_TOPIC ("bootacl"), LOG_DOM (domain),
                       "could not update domain");
    }

  signal = signals[SIGNAL_BOOTACL_CHANGED];
  pending = g_signal_has_handler_pending (domain, signal, 0, FALSE);

  if (!pending)
    return;

  if (diff_hint != NULL)
    diff = g_hash_table_ref (diff_hint);
  else
    diff = bolt_strv_diff (*acl, domain->bootacl);

  g_signal_emit (domain, signals[SIGNAL_BOOTACL_CHANGED], 0,
                 g_hash_table_size (diff) > 0, diff);
}

static gboolean
bolt_domain_bootacl_remove (BoltDomain *domain,
                            GStrv       acl,
                            const char *uuid,
                            GError    **error)
{
  char **target = NULL;

  g_return_val_if_fail (acl != NULL, FALSE);

  target = bolt_strv_contains (acl, uuid);

  if (target == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "device '%s' not in boot ACL of domain '%s'",
                   uuid, domain->id);
      return FALSE;
    }

  bolt_debug (LOG_TOPIC ("bootacl"), LOG_DOM (domain),
              "removing '%s' from bootacl", uuid);

  bolt_set_strdup (target, "");

  return TRUE;
}

static void
bolt_domain_bootacl_sync (BoltDomain *domain,
                          GStrv      *sysacl)
{
  g_autoptr(GError) err = NULL;
  g_autoptr(GPtrArray) diff = NULL;
  g_auto(GStrv) acl = NULL;
  BoltJournal *log = domain->acllog;
  gboolean ok;

  if (bolt_strv_isempty (sysacl) || log == NULL)
    return;

  bolt_info (LOG_TOPIC ("bootacl"), LOG_DOM (domain), "synchronizing journal");

  acl = g_strdupv (*sysacl);
  diff = bolt_journal_list (log, &err);

  if (diff == NULL)
    {
      bolt_warn_err (err, LOG_TOPIC ("bootacl"), LOG_DOM (domain),
                     "could not list bootacl changes");
      return;
    }

  bolt_debug (LOG_TOPIC ("bootacl"), LOG_DOM (domain),
              "journal contains %u entries", diff->len);

  for (guint i = 0; i < diff->len; i++)
    {
      BoltJournalItem *item = g_ptr_array_index (diff, i);
      BoltJournalOp op = item->op;
      const char *uid = item->id;
      ok = TRUE;

      bolt_debug (LOG_TOPIC ("bootacl"), LOG_DOM (domain),
                  "applying op '%c' for '%s'", op, uid);

      switch (op)
        {
        case BOLT_JOURNAL_ADDED:
          if (bolt_strv_contains (acl, uid))
            {
              bolt_debug (LOG_TOPIC ("bootacl"), LOG_DOM (domain),
                          "'%s' already in acl", uid);
              continue;
            }

          bolt_domain_bootacl_allocate (domain, acl, uid);
          break;

        case BOLT_JOURNAL_REMOVED:
          if (!bolt_strv_contains (acl, uid))
            {
              bolt_debug (LOG_TOPIC ("bootacl"), LOG_DOM (domain),
                          "'%s' already removed from acl", uid);
              continue;
            }

          ok = bolt_domain_bootacl_remove (domain, acl, uid, &err);
          break;

        default:
          bolt_bug (LOG_TOPIC ("bootacl"), LOG_DOM (domain),
                    "handled journal op %d", item->op);
          break;
        }

      if (!ok)
        {
          bolt_warn_err (err, LOG_TOPIC ("bootacl"), LOG_DOM (domain),
                         LOG_DEV_UID (uid),
                         "applying journal op (%d) failed for %.17s",
                         item->op, uid);

          g_clear_error (&err);
        }
    }

  ok = bolt_journal_reset (log, &err);

  if (!ok)
    {
      bolt_warn_err (err, LOG_TOPIC ("bootacl"), LOG_DOM (domain),
                     "could not reset journal");
      g_clear_error (&err);
      /* keep going */
    }

  ok = bolt_sysfs_write_boot_acl (domain->syspath, acl, &err);
  if (!ok)
    {
      bolt_warn_err (err, LOG_TOPIC ("bootacl"), LOG_DOM (domain),
                     "could not write changed bootacl to sysfs");
      return;
    }

  /* all good, we replace the passed in one with our version */
  bolt_swap (acl, *sysacl);
}

/*  */
BoltDomain *
bolt_domain_new_for_udev (struct udev_device *udev,
                          const char         *uid,
                          GError            **error)
{
  g_autoptr(GError) err = NULL;
  g_auto(GStrv) acl = NULL;
  BoltDomain *dom = NULL;
  BoltSecurity security = BOLT_SECURITY_UNKNOWN;
  const char *syspath;
  const char *sysname;
  gint sort = -1;

  g_return_val_if_fail (udev != NULL, NULL);
  g_return_val_if_fail (uid != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  if (!bolt_sysfs_device_is_domain (udev, error))
    return NULL;

  syspath = udev_device_get_syspath (udev);
  sysname = udev_device_get_sysname (udev);

  if (sysname == NULL)
    {
      g_set_error (error, BOLT_ERROR, BOLT_ERROR_UDEV,
                   "could not get domain id from udev");
      return NULL;
    }

  if (g_str_has_prefix (sysname, "domain"))
    {
      const char *ptr = sysname + strlen ("domain");
      bolt_str_parse_as_int (ptr, &sort);
    }

  security = bolt_sysfs_security_for_device (udev, error);

  if (security == BOLT_SECURITY_UNKNOWN)
    return NULL;

  acl = bolt_sysfs_read_boot_acl (udev, &err);
  if (acl == NULL && !bolt_err_notfound (err))
    bolt_warn_err (err, "failed to get boot_acl");

  dom = g_object_new (BOLT_TYPE_DOMAIN,
                      "uid", uid,
                      "id", sysname,
                      "syspath", syspath,
                      "security", security,
                      "bootacl", acl,
                      NULL);

  return dom;
}

const char *
bolt_domain_get_uid (BoltDomain *domain)
{
  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), NULL);

  return domain->uid;
}

const char *
bolt_domain_get_id (BoltDomain *domain)
{
  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), NULL);

  return domain->id;
}

const char *
bolt_domain_get_syspath (BoltDomain *domain)
{
  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), NULL);

  return domain->syspath;
}

BoltSecurity
bolt_domain_get_security (BoltDomain *domain)
{
  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), BOLT_SECURITY_UNKNOWN);

  return domain->security;
}

GStrv
bolt_domain_get_bootacl (BoltDomain *domain)
{
  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), NULL);

  return domain->bootacl;
}

GStrv
bolt_domain_dup_bootacl (BoltDomain *domain)
{
  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), NULL);

  return g_strdupv (domain->bootacl);
}

gboolean
bolt_domain_is_stored (BoltDomain *domain)
{
  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), FALSE);

  return domain->store != NULL;
}

gboolean
bolt_domain_is_connected (BoltDomain *domain)
{
  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), FALSE);

  return domain->syspath != NULL;
}

void
bolt_domain_export (BoltDomain      *domain,
                    GDBusConnection *bus)
{
  g_autoptr(GError) err  = NULL;
  BoltExported *exported;
  const char *opath;
  gboolean ok;

  exported = BOLT_EXPORTED (domain);
  ok = bolt_exported_export (exported, bus, NULL, &err);

  if (!ok)
    {
      bolt_warn_err (err, LOG_TOPIC ("dbus"),
                     "error exporting a domain");
      return;
    }

  opath = bolt_exported_get_object_path (exported);
  bolt_info (LOG_TOPIC ("dbus"), "exported domain at %s", opath);
}

void
bolt_domain_connected (BoltDomain         *domain,
                       struct udev_device *dev)
{
  g_autoptr(GError) err = NULL;
  g_auto(GStrv) acl = NULL;
  BoltSecurity security;
  const char *syspath;
  const char *id;

  g_return_if_fail (domain != NULL);
  g_return_if_fail (dev != NULL);

  id = udev_device_get_sysname (dev);
  syspath = udev_device_get_syspath (dev);

  if (domain->syspath != NULL && !bolt_streq (domain->syspath, syspath))
    {
      bolt_warn (LOG_TOPIC ("domain"), LOG_DOM (domain),
                 "already connected domain at '%s'"
                 "reconnected at '%s'", domain->syspath, syspath);

      g_free (domain->syspath);
      g_free (domain->id);
    }

  security = bolt_sysfs_security_for_device (dev, &err);

  if (security == BOLT_SECURITY_UNKNOWN)
    {
      bolt_warn_err (err, LOG_TOPIC ("udev"),
                     "error getting security from sysfs");
      g_clear_error (&err);
    }

  g_object_freeze_notify (G_OBJECT (domain));

  domain->id = g_strdup (id);
  domain->syspath = g_strdup (syspath);
  domain->security = security;

  g_object_notify_by_pspec (G_OBJECT (domain), props[PROP_ID]);
  g_object_notify_by_pspec (G_OBJECT (domain), props[PROP_SYSPATH]);
  g_object_notify_by_pspec (G_OBJECT (domain), props[PROP_SECURITY]);

  acl = bolt_sysfs_read_boot_acl (dev, &err);
  if (acl == NULL && !bolt_err_notfound (err))
    bolt_warn_err (err, "failed to get boot_acl");

  bolt_domain_bootacl_sync (domain, &acl);
  bolt_domain_bootacl_update (domain, &acl, NULL);

  g_object_thaw_notify (G_OBJECT (domain));

  bolt_msg (LOG_DOM (domain), "connected: as %s [%s] (%s)",
            id, bolt_security_to_string (security), syspath);
}

void
bolt_domain_disconnected (BoltDomain *domain)
{
  g_return_if_fail (domain != NULL);

  bolt_msg (LOG_DOM (domain), "disconnected from %s", domain->syspath);

  g_object_freeze_notify (G_OBJECT (domain));

  g_clear_pointer (&domain->id, g_free);
  g_clear_pointer (&domain->syspath, g_free);

  g_object_notify_by_pspec (G_OBJECT (domain), props[PROP_ID]);
  g_object_notify_by_pspec (G_OBJECT (domain), props[PROP_SYSPATH]);

  g_object_thaw_notify (G_OBJECT (domain));
}

void
bolt_domain_update_from_udev (BoltDomain         *domain,
                              struct udev_device *udev)
{
  g_autoptr(GError) err = NULL;
  g_auto(GStrv) acl = NULL;

  acl = bolt_sysfs_read_boot_acl (udev, &err);
  if (acl == NULL && !bolt_err_notfound (err))
    {
      bolt_warn_err (err, "failed to get boot_acl");
      return;
    }

  bolt_domain_bootacl_update (domain, &acl, NULL);
}

gboolean
bolt_domain_supports_bootacl (BoltDomain *domain)
{
  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), FALSE);

  return !bolt_strv_isempty (domain->bootacl);
}

guint
bolt_domain_bootacl_slots (BoltDomain *domain,
                           guint      *n_free)
{
  guint slots = 0;
  guint unused = 0;

  if (bolt_strv_isempty (domain->bootacl))
    {
      if (n_free)
        *n_free = 0;

      return 0;
    }

  for (char **iter = domain->bootacl; *iter; iter++)
    {
      slots++;

      if (bolt_strzero (*iter))
        unused++;
    }

  if (n_free)
    *n_free = unused;

  return slots;
}

gboolean
bolt_domain_bootacl_contains (BoltDomain *domain,
                              const char *uuid)
{
  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), FALSE);
  g_return_val_if_fail (uuid != NULL, FALSE);

  return domain->bootacl != NULL &&
         g_strv_contains ((char const * const *) domain->bootacl, uuid);
}

/* domain list management */
BoltDomain *
bolt_domain_insert (BoltDomain *list, BoltDomain *domain)
{
  BoltList iter;
  BoltList *n;

  g_return_val_if_fail (domain != NULL, list);

  /* the list as a whole takes one reference */
  g_object_ref (domain);

  if (list == NULL)
    return domain;

  bolt_nhlist_iter_init (&iter, &list->domains);
  while ((n = bolt_nhlist_iter_next (&iter)))
    {
      BoltDomain *d = bolt_list_entry (n, BoltDomain, domains);

      if (domain->sort > d->sort)
        break;
    }

  /* all existing domains are sorted before,
   * so add to the end of the list */
  if (n == NULL)
    n = list->domains.prev;

  bolt_list_add_after (n, &domain->domains);

  return list;
}

BoltDomain *
bolt_domain_remove (BoltDomain *list, BoltDomain *domain)
{
  BoltList *head;

  g_return_val_if_fail (list != NULL, NULL);
  g_return_val_if_fail (domain != NULL, list);

  head = bolt_nhlist_del (&list->domains, &domain->domains);

  /* the list as a whole has one reference, release it */
  g_object_unref (domain);

  if (head == NULL)
    return NULL;

  return bolt_list_entry (head, BoltDomain, domains);
}

const char **
bolt_domain_bootacl_get_used (BoltDomain *domain,
                              guint      *n_used)
{
  GPtrArray *res;
  guint used = 0;

  res = g_ptr_array_new ();

  for (char **iter = domain->bootacl; iter && *iter; iter++)
    {
      if (strlen (*iter))
        {
          g_ptr_array_add (res, *iter);
          used++;
        }
    }

  g_ptr_array_add (res, NULL);

  if (n_used != NULL)
    *n_used = used;

  return (const char **) g_ptr_array_free (res, FALSE);
}

void
bolt_domain_bootacl_allocate (BoltDomain *domain,
                              GStrv       acl,
                              const char *uuid)
{
  char **target;
  gboolean ok;
  gint slot = -1;

  g_return_if_fail (acl != NULL && *acl != NULL);

  /* find the first empty slot, if there is any */
  target = bolt_strv_contains (acl, "");
  if (target)
    slot = target - acl;

  bolt_debug (LOG_TOPIC ("bootacl"), LOG_DOM (domain),
              "slot before allocation: %d", slot);

  g_signal_emit (domain, signals[SIGNAL_BOOTACL_ALLOC], 0,
                 acl, uuid, &slot, &ok);

  bolt_debug (LOG_TOPIC ("bootacl"), LOG_DOM (domain),
              "slot after allocation: %d [handled: %s]",
              slot, bolt_yesno (ok));

  if (slot == -1)
    {
      /* no slot was allocated so far, lets do FIFO */
      target = bolt_strv_rotate_left (acl);
      slot = target - acl;
    }

  bolt_debug (LOG_TOPIC ("bootacl"), LOG_DOM (domain),
              "adding '%s' as bootacl[%d] (was '%s')",
              uuid, slot, acl[slot]);

  bolt_set_strdup (&acl[slot], uuid);
}

gboolean
bolt_domain_bootacl_set (BoltDomain *domain,
                         GStrv       acl,
                         GError    **error)
{
  g_autoptr(GHashTable) diff = NULL;
  g_auto(GStrv) tmp = NULL;
  BoltJournal *log;
  gboolean online;
  gboolean same;
  gboolean ok;
  guint ours, theirs;

  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), FALSE);
  g_return_val_if_fail (acl != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  ok = bolt_domain_bootacl_can_update (domain, error);
  if (!ok)
    return FALSE;

  online = domain->syspath != NULL;
  log = domain->acllog;

  theirs = g_strv_length (acl);
  ours = g_strv_length (domain->bootacl);

  if (ours != theirs)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "boot ACL length mismatch (ours: %u yours: %u)",
                   ours, theirs);
      return FALSE;
    }

  same = bolt_strv_equal (acl, domain->bootacl);

  /* NB: we return FALSE but set no error */
  if (same == TRUE)
    return FALSE;

  diff = bolt_strv_diff (domain->bootacl, acl);

  if (online)
    ok = bolt_sysfs_write_boot_acl (domain->syspath, acl, error);
  else
    ok = bolt_journal_put_diff (log, diff, error);

  if (!ok)
    return TRUE;

  tmp = g_strdupv (acl);
  bolt_domain_bootacl_update (domain, &tmp, diff);

  return TRUE;
}

gboolean
bolt_domain_bootacl_add (BoltDomain *domain,
                         const char *uuid,
                         GError    **error)
{
  g_auto(GStrv) acl = NULL;
  BoltJournal *log = NULL;
  gboolean online;
  gboolean ok;

  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), FALSE);
  g_return_val_if_fail (uuid != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  ok = bolt_domain_bootacl_can_update (domain, error);
  if (!ok)
    return FALSE;

  if (bolt_domain_bootacl_contains (domain, uuid))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS,
                   "'%s' already in boot ACL of domain '%s'",
                   uuid, domain->id);
      return FALSE;
    }

  online = domain->syspath != NULL;
  log = domain->acllog;
  acl = g_strdupv (domain->bootacl);

  bolt_domain_bootacl_allocate (domain, acl, uuid);

  if (online)
    ok = bolt_sysfs_write_boot_acl (domain->syspath, acl, error);
  else
    ok = bolt_journal_put (log, uuid, BOLT_JOURNAL_ADDED, error);

  if (!ok)
    return FALSE;

  bolt_domain_bootacl_update (domain, &acl, NULL);

  return TRUE;
}

gboolean
bolt_domain_bootacl_del (BoltDomain *domain,
                         const char *uuid,
                         GError    **error)
{
  g_autoptr(GHashTable) diff = NULL;
  g_auto(GStrv) acl = NULL;
  BoltJournal *log;
  gboolean ok;

  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), FALSE);
  g_return_val_if_fail (uuid != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  ok = bolt_domain_bootacl_can_update (domain, error);
  if (!ok)
    return FALSE;

  log = domain->acllog;
  acl = g_strdupv (domain->bootacl);

  ok = bolt_domain_bootacl_remove (domain, acl, uuid, error);

  if (!ok)
    return FALSE;

  if (domain->syspath != NULL)
    ok = bolt_sysfs_write_boot_acl (domain->syspath, acl, error);
  else
    ok = bolt_journal_put (log, uuid, BOLT_JOURNAL_REMOVED, error);

  if (!ok)
    return FALSE;

  diff = g_hash_table_new (g_str_hash, g_str_equal);
  g_hash_table_insert (diff, (gpointer) uuid, GINT_TO_POINTER ('-'));

  bolt_domain_bootacl_update (domain, &acl, diff);

  return TRUE;
}

BoltDomain *
bolt_domain_next (BoltDomain *domain)
{
  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), NULL);

  return bolt_list_entry (domain->domains.next, BoltDomain, domains);
}

BoltDomain *
bolt_domain_prev (BoltDomain *domain)
{
  g_return_val_if_fail (BOLT_IS_DOMAIN (domain), NULL);

  return bolt_list_entry (domain->domains.prev, BoltDomain, domains);
}

guint
bolt_domain_count (BoltDomain *domain)
{
  if (domain == NULL)
    return 0;

  return bolt_nhlist_len (&domain->domains);
}

void
bolt_domain_foreach (BoltDomain *list,
                     GFunc       func,
                     gpointer    data)
{
  BoltList iter;
  BoltList *n;

  if (list == NULL)
    return;

  bolt_nhlist_iter_init (&iter, &list->domains);
  while ((n = bolt_nhlist_iter_next (&iter)))
    {
      BoltDomain *d = bolt_list_entry (n, BoltDomain, domains);
      func ((gpointer) d, data);
    }
}

BoltDomain *
bolt_domain_find_id (BoltDomain *list,
                     const char *id,
                     GError    **error)
{
  BoltList iter;
  BoltList *n;

  g_return_val_if_fail (id != NULL, NULL);

  if (list == NULL)
    goto notfound;

  bolt_nhlist_iter_init (&iter, &list->domains);
  while ((n = bolt_nhlist_iter_next (&iter)))
    {
      BoltDomain *d = bolt_list_entry (n, BoltDomain, domains);
      if (bolt_streq (d->id, id) || bolt_streq (d->uid, id))
        return d;
    }

notfound:
  g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
               "domain with id '%s' could not be found.",
               id);

  return NULL;
}

void
bolt_domain_clear (BoltDomain **list)
{
  BoltDomain *iter;

  g_return_if_fail (list != NULL);

  iter = *list;

  while (bolt_domain_count (iter))
    iter = bolt_domain_remove (iter, iter);

  *list = iter;
}

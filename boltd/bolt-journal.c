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

#include "bolt-journal.h"

#include "bolt-error.h"
#include "bolt-fs.h"
#include "bolt-io.h"
#include "bolt-log.h"
#include "bolt-macros.h"
#include "bolt-str.h"

#include <gio/gunixinputstream.h>

#include <errno.h>
#include <stdio.h>

/* ************************************  */
/* BoltJournal */

static void     bolt_journal_initable_iface_init (GInitableIface *iface);

static gboolean bolt_journal_initialize (GInitable    *initable,
                                         GCancellable *cancellable,
                                         GError      **error);
struct _BoltJournal
{
  GObject  object;

  GFile   *root;
  char    *name;
  GFile   *path;

  gboolean fresh;

  int      fd;

  /* serials */
  gint64  sl_time;
  guint32 sl_count;
};


enum {
  PROP_JOURNAL_0,

  PROP_ROOT,
  PROP_NAME,

  PROP_FRESH,

  PROP_JOURNAL_LAST
};

static GParamSpec *journal_props[PROP_JOURNAL_LAST] = { NULL, };

G_DEFINE_TYPE_WITH_CODE (BoltJournal,
                         bolt_journal,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                bolt_journal_initable_iface_init));

static void
bolt_journal_finalize (GObject *object)
{
  BoltJournal *journal = BOLT_JOURNAL (object);

  if (journal->fd > -1)
    bolt_close (journal->fd, NULL);

  g_clear_object (&journal->root);
  g_clear_pointer (&journal->name, g_free);
  g_clear_object (&journal->path);

  G_OBJECT_CLASS (bolt_journal_parent_class)->finalize (object);
}

static void
bolt_journal_init (BoltJournal *journal)
{
  journal->fd = -1;
}

static void
bolt_journal_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
  BoltJournal *journal = BOLT_JOURNAL (object);

  switch (prop_id)
    {
    case PROP_ROOT:
      g_value_set_object (value, journal->root);
      break;

    case PROP_NAME:
      g_value_set_string (value, journal->name);
      break;

    case PROP_FRESH:
      g_value_set_boolean (value, journal->fresh);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bolt_journal_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
  BoltJournal *journal = BOLT_JOURNAL (object);

  switch (prop_id)
    {
    case PROP_ROOT:
      journal->root = g_value_dup_object (value);
      break;

    case PROP_NAME:
      journal->name = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bolt_journal_class_init (BoltJournalClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = bolt_journal_finalize;

  gobject_class->get_property = bolt_journal_get_property;
  gobject_class->set_property = bolt_journal_set_property;

  journal_props[PROP_ROOT] =
    g_param_spec_object ("root",
                         NULL, NULL,
                         G_TYPE_FILE,
                         G_PARAM_READWRITE      |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_NAME);

  journal_props[PROP_NAME] =
    g_param_spec_string ("name", NULL, NULL,
                         NULL,
                         G_PARAM_READWRITE      |
                         G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_NAME);

  journal_props[PROP_FRESH] =
    g_param_spec_boolean ("fresh", NULL, NULL,
                          FALSE,
                          G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class,
                                     PROP_JOURNAL_LAST,
                                     journal_props);
}

static void
bolt_journal_initable_iface_init (GInitableIface *iface)
{
  iface->init = bolt_journal_initialize;
}

static gboolean
bolt_journal_initialize (GInitable    *initable,
                         GCancellable *cancellable,
                         GError      **error)
{
  g_autoptr(GError) err = NULL;
  g_autofree char *path = NULL;
  g_autofree char *size = NULL;
  bolt_autoclose int fd = -1;
  struct stat st;
  BoltJournal *journal;
  gboolean ok;

  journal = BOLT_JOURNAL (initable);

  if (bolt_strzero (journal->name) || journal->root == NULL)
    {
      bolt_bug ("invalid arguments");
      g_set_error (error, BOLT_ERROR, BOLT_ERROR_FAILED,
                   "root and/or name NULL for journal");
      return FALSE;
    }

  journal->path = g_file_get_child (journal->root, journal->name);

  ok = bolt_fs_make_parent_dirs (journal->path, &err);
  if (!ok && !bolt_err_exists (err))
    return bolt_error_propagate (error, &err);

  path = g_file_get_path (journal->path);
  fd = bolt_open (path,
                  O_RDWR | O_APPEND | O_CREAT | O_CLOEXEC,
                  0666,
                  error);

  if (fd < 0)
    return FALSE;

  memset (&st, 0, sizeof (st));
  ok = bolt_fstat (fd, &st, &err);
  if (!ok)
    {
      g_propagate_prefixed_error (error, g_steal_pointer (&err),
                                  "could not read from journal: ");
      return FALSE;
    }

  size = g_format_size ((guint64) st.st_size);
  bolt_info (LOG_TOPIC ("journal"), "opened for '%.13s'; size: %s",
             journal->name, size);

  journal->fresh = st.st_size == 0;
  journal->fd = bolt_steal (&fd, -1);

  bolt_debug (LOG_TOPIC ("journal"), "fresh: %s, fd: %d",
              bolt_yesno (journal->fresh), journal->fd);

  return TRUE;
}

/* internal methods */
static gboolean
bolt_journal_write_entry (int           fd,
                          const char   *id,
                          BoltJournalOp op,
                          GError      **error)
{
  g_autoptr(GError) err = NULL;
  g_autofree char *data = NULL;
  const char *opstr;
  gboolean ok;
  guint64 now;
  size_t l;

  g_return_val_if_fail (fd > -1, FALSE);

  now = (guint64) g_get_real_time ();
  opstr = bolt_journal_op_to_string (op);

  data = g_strdup_printf ("%s %s %016"G_GINT64_MODIFIER "X\n",
                          id, opstr, now);

  l = strlen (data);
  ok = bolt_write_all (fd, data, l, &err);
  if (!ok)
    {
      g_propagate_prefixed_error (error, g_steal_pointer (&err),
                                  "could not add journal entry: ");
      return FALSE;
    }

  bolt_debug (LOG_TOPIC ("journal"), "wrote '%.*s' to %d",
              l - 1, data, fd);

  return TRUE;
}

static void
bolt_journal_set_fresh (BoltJournal *journal,
                        gboolean     fresh)
{
  if (journal->fresh == fresh)
    return;

  journal->fresh = fresh;
  g_object_notify_by_pspec (G_OBJECT (journal),
                            journal_props[PROP_FRESH]);
}

/* public methods */

BoltJournal *
bolt_journal_new (GFile      *root,
                  const char *name,
                  GError    **error)
{
  return g_initable_new (BOLT_TYPE_JOURNAL,
                         NULL, error,
                         "root", root,
                         "name", name,
                         NULL);
}

gboolean
bolt_journal_is_fresh (BoltJournal *journal)
{
  g_return_val_if_fail (BOLT_IS_JOURNAL (journal), FALSE);

  return journal->fresh;
}

gboolean
bolt_journal_put (BoltJournal  *journal,
                  const char   *id,
                  BoltJournalOp op,
                  GError      **error)
{
  gboolean ok;
  int r;

  g_return_val_if_fail (BOLT_IS_JOURNAL (journal), FALSE);
  g_return_val_if_fail (id != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  ok = bolt_journal_write_entry (journal->fd, id, op, error);

  if (!ok)
    return FALSE;

  r = fdatasync (journal->fd);
  if (r == -1)
    {
      bolt_warn (LOG_TOPIC ("journal"),
                 "could not flush (fdatasync) journal: %s",
                 g_strerror (errno));
    }

  bolt_journal_set_fresh (journal, FALSE);

  return TRUE;
}

gboolean
bolt_journal_put_diff (BoltJournal *journal,
                       GHashTable  *diff,
                       GError     **error)
{
  g_autoptr(GError) err = NULL;
  g_autofree char *path = NULL;
  g_autofree char *base = NULL;
  bolt_autoclose int fd = -1;
  struct stat st;
  GHashTableIter iter;
  gpointer key, val;
  gboolean ok = TRUE;

  g_return_val_if_fail (BOLT_IS_JOURNAL (journal), FALSE);
  g_return_val_if_fail (diff != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  base = g_file_get_path (journal->path);
  path = g_strdup_printf ("%s.lock", base);

  fd = bolt_open (path,
                  O_RDWR |  O_CREAT | O_CLOEXEC | O_TRUNC,
                  0666,
                  error);

  if (fd < 0)
    return FALSE;

  memset (&st, 0, sizeof (st));
  ok = bolt_fstat (journal->fd, &st, &err);
  if (!ok)
    {
      g_propagate_prefixed_error (error, g_steal_pointer (&err),
                                  "could not query journal: ");
      return FALSE;
    }

  ok = bolt_lseek (journal->fd, 0, SEEK_SET, NULL, error);

  if (ok)
    ok = bolt_copy_bytes (journal->fd, fd, st.st_size, error);

  g_hash_table_iter_init (&iter, diff);
  while (ok && g_hash_table_iter_next (&iter, &key, &val))
    {
      const char *uid = key;
      const int opcode = GPOINTER_TO_INT (val);
      BoltJournalOp op;

      switch (opcode)
        {
        case '+':
          op = BOLT_JOURNAL_ADDED;
          break;

        case '-':
          op = BOLT_JOURNAL_REMOVED;
          break;

        default:
          g_set_error (error, BOLT_ERROR, BOLT_ERROR_FAILED,
                       "unsupported op-code in diff: %c", opcode);
          return FALSE;
        }

      ok = bolt_journal_write_entry (fd, uid, op, error);
    }

  if (ok)
    ok = bolt_fdatasync (fd, error);

  if (ok)
    ok = bolt_faddflags (fd, O_APPEND, error);

  if (ok)
    ok = bolt_rename (path, base, error);

  if (!ok)
    return FALSE;

  bolt_swap (journal->fd, fd);
  bolt_journal_set_fresh (journal, FALSE);

  return TRUE;
}

GPtrArray *
bolt_journal_list (BoltJournal *journal,
                   GError     **error)
{
  g_autoptr(GInputStream) is = NULL;
  g_autoptr(GDataInputStream) ds = NULL;
  GPtrArray *res = NULL;
  off_t pos;

  g_return_val_if_fail (BOLT_IS_JOURNAL (journal), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  pos = lseek (journal->fd, 0, SEEK_SET);

  if (pos == (off_t) -1)
    {
      int code = errno;
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (code),
                   "could not read from journal: %s", g_strerror (code));
      return NULL;
    }

  res = g_ptr_array_new_full (16, (GDestroyNotify) bolt_journal_item_free);

  is = g_unix_input_stream_new (journal->fd, FALSE);
  ds = g_data_input_stream_new (is);

  g_return_val_if_fail (ds != NULL, res);

  for (;; )
    {
      g_autoptr(GError) err = NULL;
      g_autofree char *l = NULL;
      g_autofree char *name = NULL;
      g_autofree char *opstr = NULL;
      BoltJournalItem *i;
      BoltJournalOp op;
      guint64 ts;
      int n;

      l = g_data_input_stream_read_line (ds, NULL, NULL, &err);

      if (l == NULL)
        {
          if (err)
            bolt_warn_err (err, LOG_TOPIC ("journal"),
                           "error reading from journal");
          break;
        }

      n = sscanf (l, "%ms %ms %016" G_GINT64_MODIFIER "X",
                  &name, &opstr, &ts);

      if (n != 3)
        {
          bolt_warn (LOG_TOPIC ("journal"), "invalid entry: '%s'", l);
          continue;
        }

      op = bolt_journal_op_from_string (opstr, &err);

      if (err != NULL)
        {
          bolt_warn_err (err, LOG_TOPIC ("journal"),
                         "skipping entry '%s'", l);
          continue;
        }

      i = g_slice_new (BoltJournalItem);
      i->id = g_strdup (name);
      i->ts = ts;
      i->op = op;

      g_ptr_array_add (res, i);
    }

  return res;
}

gboolean
bolt_journal_reset (BoltJournal *journal,
                    GError     **error)
{
  gboolean ok;

  g_return_val_if_fail (BOLT_IS_JOURNAL (journal), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  ok = bolt_ftruncate (journal->fd, 0, error);

  if (ok)
    journal->fresh = TRUE;

  return ok;
}

/* journal op methods */
const char *
bolt_journal_op_to_string (BoltJournalOp op)
{
  switch (op)
    {
    case BOLT_JOURNAL_FAILED:
      return "!";

    case BOLT_JOURNAL_UNCHANGED:
      return "=";

    case BOLT_JOURNAL_ADDED:
      return "+";

    case BOLT_JOURNAL_REMOVED:
      return "-";
    }

  bolt_warn_enum_unhandled (BoltJournalOp, op);
  return "?";
}

BoltJournalOp
bolt_journal_op_from_string (const char *data,
                             GError    **error)
{
  g_return_val_if_fail (error == NULL || *error == NULL, BOLT_JOURNAL_FAILED);

  /* both will be caught as errors after
   * the switch statement */
  if (data == NULL)
    data = "<null>";
  else if (*data == '\0')
    data = "<empty>";

  switch (data[0])
    {
    case '!':
      return BOLT_JOURNAL_FAILED;

    case '+':
      return BOLT_JOURNAL_ADDED;

    case '-':
      return BOLT_JOURNAL_REMOVED;

    case '=':
      return BOLT_JOURNAL_UNCHANGED;
    }

  g_set_error (error, BOLT_ERROR, BOLT_ERROR_FAILED,
               "invalid journal operation: %s", data);

  return BOLT_JOURNAL_FAILED;
}

/*BoltJournalItem  */
void
bolt_journal_item_free (BoltJournalItem *entry)
{
  g_free (entry->id);
  g_slice_free (BoltJournalItem, entry);
}

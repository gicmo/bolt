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

#include "bolt-dbus.h"
#include "bolt-fs.h"
#include "bolt-test.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <fcntl.h>
#include <locale.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>


typedef struct
{
  char  *path;
  GFile *root;
} TestJournal;


static void
test_journal_setup (TestJournal *tt, gconstpointer user_data)
{
  g_autoptr(GError) error = NULL;

  tt->path = g_dir_make_tmp ("bolt.journal.XXXXXX",
                             &error);

  if (tt->path == NULL)
    {
      g_critical ("Could not create tmp dir: %s",
                  error->message);
      return;
    }

  g_debug ("journal test path at: %s", tt->path);

  tt->root = g_file_new_for_path (tt->path);
}


static void
test_journal_tear_down (TestJournal *tt, gconstpointer user_data)
{
  g_autoptr(GError) error = NULL;
  gboolean ok;

  ok = bolt_fs_cleanup_dir (tt->path, &error);

  if (!ok)
    g_warning ("Could not clean up dir: %s", error->message);

  g_clear_object (&tt->root);
  g_clear_pointer (&tt->path, g_free);
}

GLogWriterOutput
static
nonfatal_logger (GLogLevelFlags   log_level,
                 const GLogField *fields,
                 gsize            n_fields,
                 gpointer         user_data)
{
  return g_log_writer_standard_streams (log_level, fields, n_fields, user_data);
}

static void
test_journal_object (TestJournal *tt, gconstpointer user_data)
{
  g_autoptr(BoltJournal) j = NULL;
  g_autoptr(GError) err = NULL;
  g_autoptr(GFile) root = NULL;
  g_autofree char *name = NULL;
  gboolean fresh;

  if (g_test_subprocess ())
    {
      g_autofree char *path = NULL;
      int r;

      /* we fiddle with the writer, and also want to check
       * stderr, so the easy way is to do from a subprocess */
      g_log_set_writer_func (nonfatal_logger, NULL, NULL);

      /* name but no root */
      j = g_initable_new (BOLT_TYPE_JOURNAL,
                          NULL, &err,
                          "name", "test",
                          NULL);

      g_assert_error (err, BOLT_ERROR, BOLT_ERROR_FAILED);
      g_assert_null (j);
      g_clear_pointer (&err, g_error_free);


      /* root but no name */
      j = g_initable_new (BOLT_TYPE_JOURNAL,
                          NULL, &err,
                          "root", tt->root,
                          NULL);

      g_assert_error (err, BOLT_ERROR, BOLT_ERROR_FAILED);
      g_assert_null (j);
      g_clear_pointer (&err, g_error_free);

      path = g_build_filename (tt->path, "nobody", NULL);
      r = g_mkdir (path, 0755);
      g_assert_cmpint (r, >, -1);

      /* root, name, but name is an existing dir */
      j = g_initable_new (BOLT_TYPE_JOURNAL,
                          NULL, &err,
                          "root", tt->root,
                          "name", "nobody",
                          NULL);

      g_assert_error (err, G_IO_ERROR, G_IO_ERROR_IS_DIRECTORY);
      g_assert_null (j);
      g_clear_pointer (&err, g_error_free);

      return;
    }

  g_test_trap_subprocess (NULL, 0, 0);
  g_test_trap_assert_passed ();
  g_test_trap_assert_stderr ("*invalid*");

  j = bolt_journal_new (tt->root, "test", &err);

  g_assert_no_error (err);
  g_assert_nonnull (j);

  g_object_get (j,
                "name", &name,
                "root", &root,
                "fresh", &fresh,
                NULL);

  g_assert_cmpstr (name, ==, "test");
  g_assert_true (g_file_equal (root, tt->root));
  g_assert_true (fresh);
}

static void
test_journal_create (TestJournal *tt, gconstpointer user_data)
{
  g_autoptr(BoltJournal) j = NULL;
  g_autoptr(GError) err = NULL;

  /* creation and freshness check */
  j = bolt_journal_new (tt->root, "test", &err);

  g_assert_no_error (err);
  g_assert_nonnull (j);

  g_assert_true (bolt_journal_is_fresh (j));

  g_clear_object (&j);

  j = bolt_journal_new (tt->root, "test", &err);

  g_assert_no_error (err);
  g_assert_nonnull (j);

  g_assert_true (bolt_journal_is_fresh (j));
}

static void
test_journal_insert (TestJournal *tt, gconstpointer user_data)
{
  g_autoptr(BoltJournal) j = NULL;
  g_autoptr(GError) err = NULL;
  g_autoptr(GPtrArray) arr = NULL;
  gboolean ok;
  static BoltJournalItem items[] = {
    {(char *) "aaaa", BOLT_JOURNAL_ADDED,   0},
    {(char *) "bbbb", BOLT_JOURNAL_REMOVED, 0},
    {(char *) "cccc", BOLT_JOURNAL_REMOVED, 0},
    {(char *) "dddd", BOLT_JOURNAL_ADDED,   0},
    {NULL,            BOLT_JOURNAL_FAILED,  0},
  };

  j = bolt_journal_new (tt->root, "test", &err);

  g_assert_no_error (err);
  g_assert_nonnull (j);

  arr = bolt_journal_list (j, &err);
  g_assert_cmpuint (arr->len, ==, 0);

  for (BoltJournalItem *i = items; i->id; i++)
    {
      ok = bolt_journal_put (j, i->id, i->op, &err);
      g_assert_no_error (err);
      g_assert_true (ok);
    }
  g_assert_false (bolt_journal_is_fresh (j));

  g_clear_pointer (&arr, g_ptr_array_unref);
  arr = bolt_journal_list (j, &err);

  g_assert_no_error (err);
  g_assert_nonnull (arr);

  g_assert_cmpuint (arr->len, ==, G_N_ELEMENTS (items) - 1);

  for (guint i = 0; i < arr->len; i++)
    {
      BoltJournalItem *ours = items + i;
      BoltJournalItem *theirs = arr->pdata[i];

      g_assert_cmpstr (theirs->id, ==, ours->id);
      g_assert_cmpint (theirs->op, ==, ours->op);
    }

  /* close and re-open and re-do the test */
  g_clear_object (&j);
  g_clear_pointer (&arr, g_ptr_array_unref);

  j = bolt_journal_new (tt->root, "test", &err);

  g_assert_no_error (err);
  g_assert_nonnull (j);

  arr = bolt_journal_list (j, &err);

  g_assert_no_error (err);
  g_assert_nonnull (arr);

  g_assert_cmpuint (arr->len, ==, G_N_ELEMENTS (items) - 1);

  for (guint i = 0; i < arr->len; i++)
    {
      BoltJournalItem *ours = items + i;
      BoltJournalItem *theirs = arr->pdata[i];

      g_assert_cmpstr (theirs->id, ==, ours->id);
      g_assert_cmpint (theirs->op, ==, ours->op);
    }

  /* reset the journal  */
  ok = bolt_journal_reset (j, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  g_clear_pointer (&arr, g_ptr_array_unref);
  arr = bolt_journal_list (j, &err);
  g_assert_cmpuint (arr->len, ==, 0);
  g_assert_true (bolt_journal_is_fresh (j));
}

static gint
sort_journal_items (gconstpointer a,
                    gconstpointer b)
{
  const BoltJournalItem *ia = *((BoltJournalItem **) a);
  const BoltJournalItem *ib = *((BoltJournalItem **) b);

  return g_strcmp0 (ia->id, ib->id);
}

static void
test_journal_diff (TestJournal *tt, gconstpointer user_data)
{
  g_autoptr(BoltJournal) j = NULL;
  g_autoptr(GError) err = NULL;
  g_autoptr(GPtrArray) arr = NULL;
  g_autoptr(GHashTable) diff = NULL;
  gboolean ok;
  static BoltJournalItem items[] = {
    {(char *) "aaaa", BOLT_JOURNAL_ADDED,   0},
    {(char *) "bbbb", BOLT_JOURNAL_REMOVED, 0},
    {(char *) "cccc", BOLT_JOURNAL_REMOVED, 0},
    {(char *) "dddd", BOLT_JOURNAL_ADDED,   0},
    {(char *) "eeee", BOLT_JOURNAL_ADDED,   0},
    {(char *) "ffff", BOLT_JOURNAL_ADDED,   0},
  };
  guint k;

  /* bolt_journal_put_diff uses bolt_copy_bytes, which in turn uses
  * copy_file_range(2) internally, which was added in linux 4.5. */
  skip_test_unless (bolt_check_kernel_version (4, 5) || g_test_thorough (),
                    "linux kernel < 4.5, copy_file_range syscall missing");

  j = bolt_journal_new (tt->root, "diff", &err);

  /* the first and the last elements are  added "manually"
   * the rest via _put_diff */
  ok = bolt_journal_put (j, items[0].id, items[0].op, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  diff = g_hash_table_new (g_str_hash, g_str_equal);

  for (k = 1; k < G_N_ELEMENTS (items) - 2; k++)
    {
      BoltJournalItem *i = items + k;
      int op = (i->op == BOLT_JOURNAL_ADDED) ? '+' : '-';

      g_hash_table_insert (diff, i->id, GINT_TO_POINTER (op));
    }

  ok = bolt_journal_put_diff (j, diff, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  /* the last element */

  ok = bolt_journal_put (j, items[k].id, items[k].op, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  arr = bolt_journal_list (j, &err);

  g_assert_no_error (err);
  g_assert_nonnull (arr);

  g_assert_cmpuint (arr->len, ==, G_N_ELEMENTS (items) - 1);

  /* put_diff does not have an order of any of the items
   * within the diff, so they are sorted to the same order
   * as the initial array
   */
  g_ptr_array_sort (arr, sort_journal_items);

  for (guint i = 0; i < arr->len; i++)
    {
      BoltJournalItem *ours = items + i;
      BoltJournalItem *theirs = g_ptr_array_index (arr, i);

      g_assert_cmpstr (theirs->id, ==, ours->id);
      g_assert_cmpint (theirs->op, ==, ours->op);
    }
}

static void
test_journal_diff_fresh (TestJournal *tt, gconstpointer user_data)
{
  g_autoptr(BoltJournal) j = NULL;
  g_autoptr(GError) err = NULL;
  g_autoptr(GPtrArray) arr = NULL;
  g_autoptr(GHashTable) diff = NULL;
  gboolean ok;
  static BoltJournalItem items[] = {
    {(char *) "aaaa", BOLT_JOURNAL_ADDED,   0},
    {(char *) "bbbb", BOLT_JOURNAL_REMOVED, 0},
  };
  guint k;

  /* bolt_journal_put_diff uses bolt_copy_bytes, which in turn uses
  * copy_file_range(2) internally, which was added in linux 4.5. */
  skip_test_unless (bolt_check_kernel_version (4, 5) || g_test_thorough (),
                    "linux kernel < 4.5, copy_file_range syscall missing");

  j = bolt_journal_new (tt->root, "diff_fresh", &err);

  g_assert_true (bolt_journal_is_fresh (j));

  diff = g_hash_table_new (g_str_hash, g_str_equal);

  for (k = 1; k < G_N_ELEMENTS (items); k++)
    {
      BoltJournalItem *i = items + k;
      int op = (i->op == BOLT_JOURNAL_ADDED) ? '+' : '-';

      g_hash_table_insert (diff, i->id, GINT_TO_POINTER (op));
    }

  ok = bolt_journal_put_diff (j, diff, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  g_assert_false (bolt_journal_is_fresh (j));
}

static void
test_journal_invalid_file (TestJournal *tt, gconstpointer user_data)
{
  if (g_test_subprocess ())
    {
      g_autoptr(GError) err = NULL;
      g_autofree char *path = NULL;
      gboolean ok;
      const char * const invalid_data[] = {
        "justonestring\n",
        "invalidop X 0XFF",
        "str str str\n",
        "str str\n",
        "\n",
        NULL,
      };

      /* we fiddle with the writer, and also want to check
       * stderr, so the easy way is to do from a subprocess */
      g_log_set_writer_func (nonfatal_logger, NULL, NULL);

      path = g_build_filename (tt->path, "bootacl", NULL);

      for (const char *const *i = invalid_data; *i; i++)
        {
          g_autoptr(BoltJournal) j = NULL;
          g_autoptr(GPtrArray) arr = NULL;
          const char *data = *i;

          ok = g_file_set_contents (path, data, -1, &err);
          g_assert_no_error (err);
          g_assert_true (ok);

          j = bolt_journal_new (tt->root, "bootacl", &err);
          arr = bolt_journal_list (j, &err);
          g_assert_no_error (err);
          g_assert_nonnull (arr);
        }

      exit (0);
    }

  g_test_trap_subprocess (NULL, 0, 0);
  g_test_trap_assert_passed ();
  g_test_trap_assert_stderr ("*invalid entry*");
}
static void
test_journal_op_stringops (TestJournal *tt, gconstpointer user_data)
{
  g_autoptr(GError) error = NULL;
  BoltJournalOp op;
  char ops[] = "!=+-";

  /* don't include the trailing \0 */
  for (gsize i = 0; i < G_N_ELEMENTS (ops) - 1; i++)
    {
      g_autoptr(GError) err = NULL;
      char str[] = {ops[i], '\0'};
      const char *tst;

      op = bolt_journal_op_from_string (str, &err);
      g_assert_no_error (err);

      tst = bolt_journal_op_to_string (op);
      g_assert_cmpstr (tst, ==, str);
    }

  op = bolt_journal_op_from_string ("XXX", &error);
  g_assert_cmpint (op, ==, BOLT_JOURNAL_FAILED);
  g_assert_error (error, BOLT_ERROR, BOLT_ERROR_FAILED);
  g_clear_pointer (&error, g_error_free);


  op = bolt_journal_op_from_string ("", &error);
  g_assert_cmpint (op, ==, BOLT_JOURNAL_FAILED);
  g_assert_error (error, BOLT_ERROR, BOLT_ERROR_FAILED);
}

int
main (int argc, char **argv)
{

  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  bolt_dbus_ensure_resources ();

  g_test_add ("/journal/object",
              TestJournal,
              NULL,
              test_journal_setup,
              test_journal_object,
              test_journal_tear_down);

  g_test_add ("/journal/create",
              TestJournal,
              NULL,
              test_journal_setup,
              test_journal_create,
              test_journal_tear_down);

  g_test_add ("/journal/ops",
              TestJournal,
              NULL,
              test_journal_setup,
              test_journal_insert,
              test_journal_tear_down);

  g_test_add ("/journal/diff",
              TestJournal,
              NULL,
              test_journal_setup,
              test_journal_diff,
              test_journal_tear_down);

  g_test_add ("/journal/diff/fresh",
              TestJournal,
              NULL,
              test_journal_setup,
              test_journal_diff_fresh,
              test_journal_tear_down);

  g_test_add ("/journal/invalid_file",
              TestJournal,
              NULL,
              test_journal_setup,
              test_journal_invalid_file,
              test_journal_tear_down);

  g_test_add ("/journal/op/string",
              TestJournal,
              NULL,
              NULL,
              test_journal_op_stringops,
              NULL);

  return g_test_run ();
}

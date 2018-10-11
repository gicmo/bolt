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

#include "bolt-fs.h"

#include "bolt-daemon-resource.h"

#include <locale.h>

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


int
main (int argc, char **argv)
{

  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  g_resources_register (bolt_daemon_get_resource ());

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

  return g_test_run ();
}

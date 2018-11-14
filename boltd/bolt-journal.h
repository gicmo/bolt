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

#pragma once

#include <gio/gio.h>

#include <time.h>

/* BoltJournal - database for devices, keys */
#define BOLT_TYPE_JOURNAL bolt_journal_get_type ()
G_DECLARE_FINAL_TYPE (BoltJournal, bolt_journal, BOLT, JOURNAL, GObject);

typedef enum BoltJournalOp {
  BOLT_JOURNAL_FAILED    = -1,
  BOLT_JOURNAL_UNCHANGED =  '=',
  BOLT_JOURNAL_ADDED     =  '+',
  BOLT_JOURNAL_REMOVED   =  '-',
} BoltJournalOp;

typedef struct BoltJournalItem
{
  char         *id;
  BoltJournalOp op;
  guint64       ts;   /* timestamp */
} BoltJournalItem;

BoltJournal *      bolt_journal_new (GFile      *root,
                                     const char *name,
                                     GError    **error);

gboolean           bolt_journal_is_fresh (BoltJournal *journal);

gboolean           bolt_journal_put (BoltJournal  *journal,
                                     const char   *id,
                                     BoltJournalOp op,
                                     GError      **error);

gboolean           bolt_journal_put_diff (BoltJournal *journal,
                                          GHashTable  *diff,
                                          GError     **error);

GPtrArray *        bolt_journal_list (BoltJournal *journal,
                                      GError     **error);

gboolean           bolt_journal_reset (BoltJournal *journal,
                                       GError     **error);

/* BoltJournalOp */
const char *      bolt_journal_op_to_string (BoltJournalOp op);

BoltJournalOp     bolt_journal_op_from_string (const char *data,
                                               GError    **error);
/* BoltJournalItem */
void               bolt_journal_item_free (BoltJournalItem *entry);

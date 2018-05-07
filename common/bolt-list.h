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

#include <glib.h>

#include "bolt-macros.h"

G_BEGIN_DECLS

typedef struct BoltList_ BoltList;
struct BoltList_
{
  BoltList *next;
  BoltList *prev;
};

static inline void
bolt_list_init (BoltList *node)
{
  node->next = node;
  node->prev = node;
}

static inline void
bolt_list_add_internal (BoltList *prev, BoltList *next, BoltList *node)
{
  node->next = next;
  node->prev = prev;
  next->prev = node;
  prev->next = node;
}

static inline BoltList *
bolt_list_add_before (BoltList *pos, BoltList *node)
{
  if (pos == NULL)
    return node;

  bolt_list_add_internal (pos->prev, pos, node);

  return pos;
}

static inline BoltList *
bolt_list_add_after (BoltList *pos, BoltList *node)
{
  if (pos == NULL)
    return node;

  bolt_list_add_internal (pos, pos->next, node);

  return pos;
}

static inline void
bolt_list_del_internal (BoltList *prev, BoltList *next)
{
  prev->next = next;
  next->prev = prev;
}

#define bolt_list_entry(node, type, member) bolt_container_of (node, type, member)

/* no head list */
static inline guint
bolt_nhlist_len (BoltList *list)
{
  BoltList *i = list;
  guint count = 0;

  if (list == NULL)
    return count;

  do
    {
      count++;
      i = i->next;
    }
  while (i != list);

  return count;
}

static inline BoltList *
bolt_nhlist_del (BoltList *list, BoltList *node)
{
  if (node == NULL || list == NULL)
    return list;

  bolt_list_del_internal (node->prev, node->next);

  if (node == list)
    {
      if (list->next == list)
        return NULL;
      else
        return list->next;
    }

  return list;
}

/*
 * Using a BoltList struct an iterator, where:
 *   next    the *current* node
 *   prev    the *head* of the list
 *
 * The following special conditions are encoded:
 * sym | next  |  prev  |  state
 *  S  | *     |  NULL  |  first iteration, initial state
 *  E  | NULL  |  *     |  end of iteration, final state
 */
static inline BoltList *
bolt_nhlist_iter_init (BoltList *iter, BoltList *list)
{
  iter->next = list;
  iter->prev = NULL;

  return iter;
}

#define bolt_nhlist_iter_head(iter_) ((iter_)->prev ? : (iter_)->next)
#define bolt_nhlist_iter_node(iter_) ((iter_ != NULL) ? (iter_)->next : NULL)
#define bolt_nhlist_iter_entry(iter_, type, member) \
  bolt_container_of (bolt_nhlist_iter_node (iter_), type, member)

static inline BoltList *
bolt_nhlist_iter_next (BoltList *iter)
{
  g_return_val_if_fail (iter != NULL, NULL);

  if (iter->next == NULL) /* E */
    return NULL;

  if (iter->prev == NULL) /* S */
    {
      iter->prev = iter->next;
      return iter->next;
    }

  iter->next = iter->next->next;

  if (iter->next == iter->prev)
    iter->next = NULL;

  return iter->next;
}

G_END_DECLS

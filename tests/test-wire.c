/*
 * Copyright © 2019 Red Hat, Inc
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

#include "bolt-wire.h"

#include "bolt-error.h"
#include "test-enums.h"
#include "bolt-test-resources.h"

#include <glib.h>
#include <gio/gio.h>
#include <glib/gprintf.h>

#include <locale.h>

typedef struct TestWire
{
  int dummy;
} TestWire;

static void
test_linkspeed_basic (TestWire *tt, gconstpointer data)
{
  BoltLinkSpeed a =  {
    .rx.speed = 10,
    .rx.lanes = 1,
    .tx.speed = 20,
    .tx.lanes = 2
  };
  BoltLinkSpeed b = {
    .rx.speed = 20,
    .rx.lanes = 2,
    .tx.speed = 10,
    .tx.lanes = 1
  };
  g_autofree BoltLinkSpeed *c = NULL;

  g_assert_false (bolt_link_speed_equal (&a, &b));
  b = a;
  g_assert_true (bolt_link_speed_equal (&a, &b));

  c = bolt_link_speed_copy (&a);
  g_assert_true (bolt_link_speed_equal (&a, c));
}

int
main (int argc, char **argv)
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  g_test_add ("/common/linkseed/basic",
              TestWire,
              NULL,
              NULL,
              test_linkspeed_basic,
              NULL);

  return g_test_run ();
}

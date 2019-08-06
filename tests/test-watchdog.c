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

#include "bolt-watchdog.h"

#include <locale.h>

typedef struct TestWatchdog
{
  int dummy;
} TestWatchdog;

static void
test_watchdog_basic (TestWatchdog *tt, gconstpointer user_data)
{
  g_autoptr(GError) err = NULL;
  g_autoptr(BoltWatchdog) dog = NULL;

  if (!g_test_slow ())
    {
      g_test_skip ("slow tests disabled");
      return;
    }

  dog = bolt_watchdog_new (&err);
  g_assert_no_error (err);
  g_assert_nonnull (dog);
}

int
main (int argc, char **argv)
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  g_test_add ("/boltd/watchdog/basic",
              TestWatchdog,
              NULL,
              NULL,
              test_watchdog_basic,
              NULL);

  return g_test_run ();
}

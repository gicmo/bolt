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

#include "bolt-unix.h"

#include <locale.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h> /* unlinkat, fork */

typedef struct TestDummy
{
  int dummy;
} TestDummy;

static void
test_pid_is_alive (TestDummy *tt, gconstpointer user_data)
{
  gboolean ok;
  pid_t p;
  pid_t r;
  int status;

  ok = bolt_pid_is_alive (0);
  g_assert_true (ok);

  p = fork ();
  g_assert_cmpint ((int) p, >, -1);

  if (p == 0)
    /* child */
    exit (42);
  /* parent */
  ok = bolt_pid_is_alive (p);
  g_assert_true (ok);

  r = waitpid (0, &status, 0);
  g_assert_cmpint ((int) r, ==, (int) p);

  ok = bolt_pid_is_alive (p);
  g_assert_false (ok);
}


int
main (int argc, char **argv)
{

  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  g_test_add ("/common/unix/pid_is_alive",
              TestDummy,
              NULL,
              NULL,
              test_pid_is_alive,
              NULL);

  return g_test_run ();
}

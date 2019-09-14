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

#include <bolt-test.h>

#include <locale.h>

typedef int TestDummy;

static void
test_version_parse (TestDummy *tt, gconstpointer user_data)
{
  struct VersionTest
  {
    const char *str;

    /* result */
    gboolean ok;

    /* data */
    int         major;
    int         minor;
    int         patch;

    const char *suffix;
  } ftt[] = {
    {"",                       FALSE, -1, -1, -1,  NULL},
    {"parsererror",            FALSE, -1, -1, -1,  NULL},
    {"parser.err.or",          FALSE, -1, -1, -1,  NULL},
    {"1.0.0.43",               FALSE,  1,  0, -1,  NULL},
    {"1",                      TRUE,   1, -1, -1,  NULL},
    {"1.0",                    TRUE,   1,  0, -1,  NULL},
    {"1.0.0",                  TRUE,   1,  0,  0,  NULL},
    {"1-100",                  TRUE,   1, -1, -1,  "100"},
    {"1-100.fc",               TRUE,   1, -1, -1,  "100.fc"},
    {"1.0-100.fc",             TRUE,   1,  0, -1,  "100.fc"},
    {"1.0.0-100.fc",           TRUE,   1,  0,  0,  "100.fc"},
    {"5.2.11-200.fc30.x86_64", TRUE,   5,  2,  11, "200.fc30.x86_64"},
    {"4.4.0-161-generic",      TRUE,   4,  4,  0,  "161-generic"}
  };

  for (guint i = 0; i < G_N_ELEMENTS (ftt); i++)
    {
      g_autoptr(GError) err = NULL;
      g_auto(BoltVersion) v = { .major = 42, .minor = 42, .patch = 42, .suffix = NULL};
      gboolean ok;

      ok = bolt_version_parse (ftt[i].str, &v, &err);

      if (ftt[i].ok)
        {
          g_assert_no_error (err);
          g_assert_true (ok);
        }
      else
        {
          g_assert_nonnull (err);
          g_assert_false (ok);
        }

      g_assert_cmpint (v.major, ==, ftt[i].major);
      g_assert_cmpint (v.minor, ==, ftt[i].minor);
      g_assert_cmpint (v.patch, ==, ftt[i].patch);

      if (ftt[i].suffix)
        g_assert_cmpstr (v.suffix, ==, ftt[i].suffix);
      else
        g_assert_null (v.suffix);
    }
}

int
main (int argc, char **argv)
{
  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  g_test_add ("/self/version/parse",
              TestDummy,
              NULL,
              NULL,
              test_version_parse,
              NULL);

  return g_test_run ();
}

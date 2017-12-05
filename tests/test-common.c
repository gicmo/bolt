/*
 * Copyright © 2017 Red Hat, Inc
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

#include "bolt-enums.h"
#include "bolt-error.h"
#include "bolt-fs.h"
#include "bolt-io.h"
#include "bolt-rnd.h"
#include "bolt-str.h"

#include <glib.h>
#include <gio/gio.h>
#include <glib/gprintf.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h> /* unlinkat */

static void
cleanup_dir (DIR *d)
{
  struct dirent *de = NULL;

  for (errno = 0, de = readdir (d); de != NULL; errno = 0, de = readdir (d))
    {
      g_autoptr(GError) error = NULL;
      int uflag               = 0;

      if (!g_strcmp0 (de->d_name, ".") ||
          !g_strcmp0 (de->d_name, ".."))
        continue;

      if (de->d_type == DT_DIR)
        {
          g_autoptr(DIR) cd = NULL;

          cd = bolt_opendir_at (dirfd (d), de->d_name, O_RDONLY, &error);
          if (cd == NULL)
            continue;

          cleanup_dir (cd);
          uflag = AT_REMOVEDIR;
        }

      unlinkat (dirfd (d), de->d_name, uflag);
    }
}


typedef struct
{
  int dummy;
} TestRng;

static void
test_enums (TestRng *tt, gconstpointer user_data)
{
  g_assert_cmpstr (bolt_security_to_string (BOLT_SECURITY_NONE), ==, "none");
  g_assert_cmpstr (bolt_security_to_string (BOLT_SECURITY_DPONLY), ==, "dponly");
  g_assert_cmpstr (bolt_security_to_string (BOLT_SECURITY_USER), ==, "user");
  g_assert_cmpstr (bolt_security_to_string (BOLT_SECURITY_SECURE), ==, "secure");

  g_assert_cmpuint (bolt_security_from_string ("none"), ==, BOLT_SECURITY_NONE);
  g_assert_cmpuint (bolt_security_from_string ("dponly"), ==, BOLT_SECURITY_DPONLY);
  g_assert_cmpuint (bolt_security_from_string ("user"), ==, BOLT_SECURITY_USER);
  g_assert_cmpuint (bolt_security_from_string ("secure"), ==, BOLT_SECURITY_SECURE);
}

typedef void (*rng_t) (void *buf,
                       gsize n);

#define RNG_COUNT 258
static guint
test_rng_loop (guint N, rng_t fn)
{
  char buf[RNG_COUNT] = { 0, };
  guint count[RNG_COUNT] = {0, };
  guint hits = 0;

  for (guint n = 0; n < N; n++)
    {
      memset (buf, 0, sizeof (buf));
      fn (buf, sizeof (buf));

      for (guint i = 0; i < RNG_COUNT; i++)
        if (buf[i] == 0)
          count[i]++;
    }

  for (guint i = 0; i < RNG_COUNT; i++)
    hits = MAX (hits, count[i]);

  return hits;
}

static void
no_rng (void *buf, gsize n)
{
  /* noop  */
}

#if HAVE_FN_GETRANDOM
static void
getrandom_rng (void *buf, gsize n)
{
  g_autoptr(GError) error = NULL;
  gboolean ok;

  ok = bolt_random_getrandom (buf, n, 0, &error);
  g_assert_no_error (error);
  g_assert_true (ok);
}
#endif

static void
test_rng (TestRng *tt, gconstpointer user_data)
{
  char buf[10] = {0, };
  guint hits;
  gboolean ok;
  static guint N = 10;

  hits = test_rng_loop (N, no_rng);
  g_assert_cmpuint (hits, ==, 10);

  hits = test_rng_loop (N, bolt_random_prng);
  g_assert_cmpuint (hits, <, 10);

  hits = test_rng_loop (N, (rng_t) bolt_get_random_data);
  g_assert_cmpuint (hits, <, 10);

  ok = bolt_random_urandom (buf, sizeof (buf));
  if (ok)
    {
      hits = test_rng_loop (N, (rng_t) bolt_random_urandom);
      g_assert_cmpuint (hits, <, 10);
    }
  else
    {
      g_debug ("urandom RNG seems to not be working");
    }


#if HAVE_FN_GETRANDOM
  g_debug ("testing getrandom");
  hits = test_rng_loop (N, (rng_t) getrandom_rng);
  g_assert_cmpuint (hits, <, 10);
#else
  g_debug ("getrandom RNG not available");
#endif

}

static void
test_erase (TestRng *tt, gconstpointer user_data)
{
  char buf[256] = {0, };
  char *d1, *d2, *n0 = NULL;
  size_t n;

  bolt_get_random_data (buf, sizeof (buf) - 1);
  d1 = g_strdup (buf);
  d2 = g_strdup (buf);

  g_assert_nonnull (d2);
  g_assert_nonnull (d2);

  /* make sure we don't crash on NULL */
  bolt_str_erase (NULL);
  bolt_str_erase (n0);
  bolt_str_erase_clear (&n0);

  bolt_str_erase_clear (&d2);
  g_assert_null (d2);

  n = strlen (d1);
  bolt_str_erase (d1);
  g_assert_cmpstr (d1, !=, buf);

  g_assert_cmpuint (strlen (d1), ==, 0);
  for (guint i = 0; i < n; i++)
    g_assert_cmpint (d1[i], ==, 0);

  bolt_erase_n (buf, sizeof (buf));
  for (guint i = 0; i < n; i++)
    g_assert_cmpint (buf[i], ==, 0);
}


typedef struct
{
  char *path;
} TestIO;

static void
test_io_setup (TestIO *tt, gconstpointer user_data)
{
  g_autoptr(GError) error = NULL;

  tt->path = g_dir_make_tmp ("bolt.io.XXXXXX",
                             &error);

  if (tt->path == NULL)
    {
      g_critical ("Could not create tmp dir: %s",
                  error->message);
      return;
    }

}

static void
test_io_tear_down (TestIO *tt, gconstpointer user_data)
{
  g_autoptr(DIR) d = NULL;
  g_autoptr(GError) error = NULL;

  d = bolt_opendir (tt->path, &error);

  if (d)
    {
      g_debug ("Cleaning up: %s", tt->path);
      cleanup_dir (d);
    }
  else if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
    {
      g_warning ("Could not clean up dir: %s", error->message);
    }
}


static const char *valid_uid = "f96b4cc77f196068ec454cb6006514c602d1011f47dd275cf5c6b8a47744f049";

static void
test_io_verify (TestIO *tt, gconstpointer user_data)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(DIR) d = NULL;
  g_autofree char *uid_path = NULL;
  gboolean ok;

  uid_path = g_build_filename (tt->path, "unique_id", NULL);

  ok = g_file_set_contents (uid_path,
                            "wrong_to_small",
                            -1,
                            &error);
  g_assert_true (ok);
  g_assert_no_error (error);
  d = bolt_opendir (tt->path, &error);

  g_assert_nonnull (d);
  g_assert_no_error (error);

  /* must fail */
  ok = bolt_verify_uid (dirfd (d), valid_uid, &error);
  g_assert_false (ok);
  g_assert_error (error, BOLT_ERROR, BOLT_ERROR_FAILED);
  g_clear_error (&error);


  ok = g_file_set_contents (uid_path,
                            valid_uid,
                            -1,
                            &error);
  g_assert_true (ok);
  g_assert_no_error (error);

  /* must work */
  ok = bolt_verify_uid (dirfd (d), valid_uid, &error);
  g_assert_true (ok);
  g_assert_no_error (error);
  g_clear_error (&error);

  unlinkat (dirfd (d), "unique_id", 0);
}

static void
test_fs (TestIO *tt, gconstpointer user_data)
{
  g_autoptr(GFile) base = NULL;
  g_autoptr(GFile) dir = NULL;
  g_autoptr(GFile) target = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *path = NULL;
  gboolean ok;

  base = g_file_new_for_path (tt->path);

  dir = g_file_get_child (base, "in/a/galaxy/far/far");
  target = g_file_get_child (dir, "luke");

  ok = bolt_fs_make_parent_dirs (target, &error);
  g_assert_no_error (error);
  g_assert_true (ok);

  ok = g_file_query_exists (target, NULL);
  g_assert_false (ok);

  ok = g_file_query_exists (dir, NULL);
  g_assert_true (ok);

  ok = bolt_fs_make_parent_dirs (target, &error);
  g_assert_no_error (error);
  g_assert_true (ok);

  ok = bolt_fs_make_parent_dirs (dir, &error);
  g_assert_no_error (error);
  g_assert_true (ok);

  g_clear_object (&target);
  target = g_file_get_child (base, "darth");

  path = g_file_get_path (target);
  ok = g_file_set_contents (path, "vader", -1, &error);
  g_assert_no_error (error);
  g_assert_true (ok);

  ok = g_file_query_exists (target, NULL);
  g_assert_true (ok);

  ok = bolt_fs_make_parent_dirs (target, &error);
  g_assert_no_error (error);
  g_assert_true (ok);
}

static void
test_str (TestRng *tt, gconstpointer user_data)
{
  GPtrArray *a = NULL;
  GStrv r;

  r = bolt_strv_from_ptr_array (NULL);
  g_assert_null (r);

  r = bolt_strv_from_ptr_array (&a);
  g_assert_null (r);

  a = g_ptr_array_new ();
  r = bolt_strv_from_ptr_array (&a);
  g_assert_nonnull (r);
  g_assert_null (a);

  g_assert_cmpuint (0U, ==, g_strv_length (r));
  g_strfreev (r);

  a = g_ptr_array_new ();
  g_ptr_array_add (a, NULL);
  r = bolt_strv_from_ptr_array (&a);
  g_assert_nonnull (r);
  g_assert_null (a);

  g_assert_cmpuint (0U, ==, g_strv_length (r));
  g_strfreev (r);

  a = g_ptr_array_new ();
  g_ptr_array_add (a, g_strdup ("test"));
  r = bolt_strv_from_ptr_array (&a);
  g_assert_nonnull (r);
  g_assert_null (a);

  g_assert_cmpuint (1U, ==, g_strv_length (r));
  g_assert_true (g_strv_contains ((char const * const * ) r, "test"));
  g_strfreev (r);
}


static void
test_str_erase (TestRng *tt, gconstpointer user_data)
{
  char buf[256] = {0, };
  char *d1, *d2, *n0 = NULL;
  size_t n;

  bolt_get_random_data (buf, sizeof (buf) - 1);
  d1 = g_strdup (buf);
  d2 = g_strdup (buf);

  g_assert_nonnull (d2);
  g_assert_nonnull (d2);

  /* make sure we don't crash on NULL */
  bolt_str_erase (NULL);
  bolt_str_erase (n0);
  bolt_str_erase_clear (&n0);

  bolt_str_erase_clear (&d2);
  g_assert_null (d2);

  n = strlen (d1);
  bolt_str_erase (d1);
  g_assert_cmpstr (d1, !=, buf);

  g_assert_cmpuint (strlen (d1), ==, 0);
  for (guint i = 0; i < n; i++)
    g_assert_cmpint (d1[i], ==, 0);

  bolt_erase_n (buf, sizeof (buf));
  for (guint i = 0; i < n; i++)
    g_assert_cmpint (buf[i], ==, 0);
}

int
main (int argc, char **argv)
{

  setlocale (LC_ALL, "");

  g_test_init (&argc, &argv, NULL);

  g_test_add ("/common/enums",
              TestRng,
              NULL,
              NULL,
              test_enums,
              NULL);

  g_test_add ("/common/rng",
              TestRng,
              NULL,
              NULL,
              test_rng,
              NULL);

  g_test_add ("/common/erase",
              TestRng,
              NULL,
              NULL,
              test_erase,
              NULL);

  g_test_add ("/common/io/verify",
              TestIO,
              NULL,
              test_io_setup,
              test_io_verify,
              test_io_tear_down);

  g_test_add ("/common/fs",
              TestIO,
              NULL,
              test_io_setup,
              test_fs,
              test_io_tear_down);

  g_test_add ("/common/str",
              TestRng,
              NULL,
              NULL,
              test_str,
              NULL);

  g_test_add ("/common/str/erase",
              TestRng,
              NULL,
              NULL,
              test_str_erase,
              NULL);

  return g_test_run ();
}

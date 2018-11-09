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
#include "bolt-list.h"
#include "bolt-rnd.h"
#include "bolt-str.h"
#include "mock-sysfs.h"

#include "test-enums.h"

#include <glib.h>
#include <gio/gio.h>
#include <glib/gprintf.h>

#include <fcntl.h>
#include <locale.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h> /* unlinkat */

#if !GLIB_CHECK_VERSION (2, 57, 0)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GEnumClass, g_type_class_unref);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GFlagsClass, g_type_class_unref);
#endif


typedef struct
{
  int dummy;
} TestRng;

static void
test_enums (TestRng *tt, gconstpointer user_data)
{
  g_autoptr(GEnumClass) klass;
  g_autoptr(GError) err = NULL;
  const char *str;
  gint val;
  gboolean ok;
  struct EnumTest
  {
    GType       enum_type;
    const char *name;
    gint        value;

  } ett[] = {
    {BOLT_TYPE_SECURITY, "none",   BOLT_SECURITY_NONE},
    {BOLT_TYPE_SECURITY, "dponly", BOLT_SECURITY_DPONLY},
    {BOLT_TYPE_SECURITY, "user",   BOLT_SECURITY_USER},
    {BOLT_TYPE_SECURITY, "secure", BOLT_SECURITY_SECURE},
  };

  for (guint i = 0; i < G_N_ELEMENTS (ett); i++)
    {

      ok = bolt_enum_validate (ett[i].enum_type, ett[i].value, &err);
      g_assert_no_error (err);
      g_assert_true (ok);

      /* to string */
      str = bolt_enum_to_string (ett[i].enum_type, ett[i].value, &err);
      g_assert_no_error (err);
      g_assert_nonnull (str);
      g_assert_cmpstr (str, ==, ett[i].name);

      /* from string */
      val = bolt_enum_from_string (ett[i].enum_type, ett[i].name, &err);
      g_assert_no_error (err);
      g_assert_cmpint (val, ==, ett[i].value);
    }

  g_assert_cmpstr (bolt_security_to_string (BOLT_SECURITY_NONE), ==, "none");
  g_assert_cmpstr (bolt_security_to_string (BOLT_SECURITY_DPONLY), ==, "dponly");
  g_assert_cmpstr (bolt_security_to_string (BOLT_SECURITY_USER), ==, "user");
  g_assert_cmpstr (bolt_security_to_string (BOLT_SECURITY_SECURE), ==, "secure");

  g_assert_cmpuint (bolt_security_from_string ("none"), ==, BOLT_SECURITY_NONE);
  g_assert_cmpuint (bolt_security_from_string ("dponly"), ==, BOLT_SECURITY_DPONLY);
  g_assert_cmpuint (bolt_security_from_string ("user"), ==, BOLT_SECURITY_USER);
  g_assert_cmpuint (bolt_security_from_string ("secure"), ==, BOLT_SECURITY_SECURE);

  klass = g_type_class_ref (BOLT_TYPE_SECURITY);

  ok = bolt_enum_class_validate (klass, klass->minimum, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  ok = bolt_enum_class_validate (klass, klass->maximum, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  str = bolt_enum_to_string (BOLT_TYPE_SECURITY, klass->minimum, &err);
  g_assert_no_error (err);
  g_assert_nonnull (str);

  str = bolt_enum_to_string (BOLT_TYPE_SECURITY, klass->maximum, &err);
  g_assert_no_error (err);
  g_assert_nonnull (str);

  ok = bolt_enum_class_validate (klass, klass->maximum + 1, &err);
  g_assert_nonnull (err);
  g_assert_false (ok);
  g_clear_error (&err);

  ok = bolt_enum_class_validate (klass, klass->minimum - 1, &err);
  g_assert_nonnull (err);
  g_assert_false (ok);
  g_clear_error (&err);

  ok = bolt_enum_validate (BOLT_TYPE_SECURITY, -42, &err);
  g_assert_nonnull (err);
  g_assert_false (ok);
  g_clear_error (&err);

  str = bolt_enum_to_string (BOLT_TYPE_SECURITY, -42, &err);
  g_assert_nonnull (err);
  g_assert_null (str);
  g_clear_error (&err);

  val = bolt_enum_from_string (BOLT_TYPE_SECURITY, "ILEDELI", &err);
  g_assert_nonnull (err);
  g_assert_cmpint (val, ==, -1);
  g_clear_error (&err);
}

static void
test_error (TestRng *tt, gconstpointer user_data)
{
  g_autoptr(GError) failed = NULL;
  g_autoptr(GError) notfound = NULL;
  g_autoptr(GError) exists = NULL;
  g_autoptr(GError) inval = NULL;
  g_autoptr(GError) cancelled = NULL;
  g_autoptr(GError) badstate = NULL;
  g_autoptr(GError) target = NULL;
  g_autoptr(GError) source = NULL;
  g_autoptr(GError) noerror = NULL;
  g_autoptr(GError) buserr = NULL;
  g_autofree char *remote = NULL;
  gboolean ok;

  g_set_error_literal (&failed, BOLT_ERROR, BOLT_ERROR_FAILED,
                       "operation failed");

  /* bolt_err_notfound */
  g_set_error_literal (&notfound, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                       "not found");

  g_assert_false (bolt_err_notfound (failed));
  g_assert_true (bolt_err_notfound (notfound));

  /* bolt_err_exists */
  g_set_error_literal (&exists, G_IO_ERROR, G_IO_ERROR_EXISTS,
                       "already exists");

  g_assert_false (bolt_err_exists (failed));
  g_assert_true (bolt_err_exists (exists));

  /* (bolt_err_inval */
  g_set_error_literal (&inval, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                       "invalid argument");

  g_assert_false (bolt_err_inval (failed));
  g_assert_true (bolt_err_inval (inval));

  /* bolt_err_cancelled */
  g_set_error_literal (&cancelled, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                       "cancelled");

  g_assert_false (bolt_err_cancelled (failed));
  g_assert_true (bolt_err_cancelled (cancelled));

  /* bolt_err_badstate */
  g_set_error_literal (&badstate, BOLT_ERROR, BOLT_ERROR_BADSTATE,
                       "bad state");

  g_assert_false (bolt_err_badstate (failed));
  g_assert_true (bolt_err_badstate (badstate));


  /* bolt_error_propagate */
  ok = bolt_error_propagate (NULL, &noerror);
  g_assert_true (ok);

  g_assert_no_error (target);
  ok = bolt_error_propagate (&target, &noerror);
  g_assert_no_error (target);
  g_assert_true (ok);

  g_set_error_literal (&source, BOLT_ERROR, BOLT_ERROR_FAILED,
                       "operation failed");
  g_assert_no_error (target);
  g_assert_error (source, BOLT_ERROR, BOLT_ERROR_FAILED);

  ok = bolt_error_propagate (&target, &source);
  g_assert_no_error (source);
  g_assert_error (target, BOLT_ERROR, BOLT_ERROR_FAILED);
  g_assert_false (ok);

  /*     and back */
  ok = bolt_error_propagate (&source, &target);
  g_assert_no_error (target);
  g_assert_error (source, BOLT_ERROR, BOLT_ERROR_FAILED);
  g_assert_false (ok);

  /* bolt_error_propagate_stripped */
  ok = bolt_error_propagate_stripped (NULL, &noerror);
  g_assert_true (ok);

  g_assert_no_error (target);
  ok = bolt_error_propagate_stripped (&target, &noerror);
  g_assert_no_error (target);
  g_assert_true (ok);

  /*     normal error */
  ok = bolt_error_propagate_stripped (&target, &source);
  g_assert_no_error (source);
  g_assert_error (target, BOLT_ERROR, BOLT_ERROR_FAILED);
  g_assert_false (ok);

  g_clear_pointer (&target, g_error_free);

  /*     bus error */
  g_set_error_literal (&buserr, BOLT_ERROR, BOLT_ERROR_BADKEY,
                       "such a bad, bad key");

  remote = g_dbus_error_get_remote_error (buserr);
  source = g_dbus_error_new_for_dbus_error (remote, buserr->message);
  g_assert_error (source, BOLT_ERROR, BOLT_ERROR_BADKEY);
  g_assert_true (g_dbus_error_is_remote_error (source));

  ok = bolt_error_propagate_stripped (&target, &source);
  g_assert_error (target, BOLT_ERROR, BOLT_ERROR_BADKEY);
  g_assert_false (g_dbus_error_is_remote_error (target));
  g_assert_cmpstr (target->message, ==, buserr->message);
}

static void
test_flags (TestRng *tt, gconstpointer user_data)
{
  g_autoptr(GFlagsClass) klass;
  g_autoptr(GError) err = NULL;
  char *str;
  guint val;
  guint ref;
  gboolean ok;
  gboolean chg;
  struct EnumTest
  {
    GType       flags_type;
    const char *name;
    guint       value;

  } ftt[] = {
    {BOLT_TYPE_KITT_FLAGS, "disabled",    BOLT_KITT_DISABLED},
    {BOLT_TYPE_KITT_FLAGS, "enabled",     BOLT_KITT_ENABLED},
    {BOLT_TYPE_KITT_FLAGS, "sspm",        BOLT_KITT_SSPM},
    {BOLT_TYPE_KITT_FLAGS, "turbo-boost", BOLT_KITT_TURBO_BOOST},
    {BOLT_TYPE_KITT_FLAGS, "ski-mode",    BOLT_KITT_SKI_MODE},

    {BOLT_TYPE_KITT_FLAGS,
     "enabled | ski-mode",
     BOLT_KITT_ENABLED | BOLT_KITT_SKI_MODE},

    {BOLT_TYPE_KITT_FLAGS,
     "sspm | turbo-boost | ski-mode",
     BOLT_KITT_SSPM | BOLT_KITT_SKI_MODE | BOLT_KITT_TURBO_BOOST},

  };

  for (guint i = 0; i < G_N_ELEMENTS (ftt); i++)
    {
      g_autofree char *s = NULL;

      /* to string */
      s = bolt_flags_to_string (ftt[i].flags_type, ftt[i].value, &err);
      g_assert_no_error (err);
      g_assert_nonnull (s);
      g_assert_cmpstr (s, ==, ftt[i].name);

      /* from string */
      ok = bolt_flags_from_string (ftt[i].flags_type, ftt[i].name, &val, &err);
      g_assert_no_error (err);
      g_assert_true (ok);
      g_assert_cmpuint (val, ==, ftt[i].value);
    }

  /* handle NULL for flags class */
  ok = bolt_flags_class_from_string (NULL, "fax-machine", &val, &err);
  g_assert_error (err, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS);
  g_assert_false (ok);
  g_clear_error (&err);

  str = bolt_flags_class_to_string (NULL, 0xFFFF, &err);
  g_assert_error (err, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS);
  g_assert_null (str);
  g_clear_error (&err);

  /* handle invalid values */
  klass = g_type_class_ref (BOLT_TYPE_KITT_FLAGS);

  ok = bolt_flags_class_from_string (klass, NULL, &val, &err);
  g_assert_error (err, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS);
  g_assert_false (ok);
  g_clear_error (&err);

  ok = bolt_flags_class_from_string (klass, "fax-machine", &val, &err);
  g_assert_error (err, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS);
  g_assert_false (ok);
  g_clear_error (&err);

  str = bolt_flags_class_to_string (klass, 0xFFFF, &err);
  g_assert_error (err, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS);
  g_assert_null (str);
  g_clear_error (&err);

  /* there and back again */
  ref = BOLT_KITT_SSPM | BOLT_KITT_SKI_MODE | BOLT_KITT_TURBO_BOOST;
  str = bolt_flags_class_to_string (klass, ref, &err);
  g_assert_no_error (err);
  g_assert_nonnull (str);

  ok = bolt_flags_class_from_string (klass, str, &val, &err);
  g_assert_no_error (err);
  g_assert_true (ok);
  g_assert_cmpuint (val, ==, ref);

  g_clear_pointer (&str, g_free);

  /* handle "" and 0 */
  ok = bolt_flags_class_from_string (klass, "", &val, &err);
  g_assert_no_error (err);
  g_assert_true (ok);
  g_assert_cmpuint (val, ==, BOLT_KITT_DISABLED);

  str = bolt_flags_class_to_string (klass, 0, &err);
  g_assert_no_error (err);
  g_assert_nonnull (str);
  g_assert_cmpstr (str, ==, "disabled");
  g_clear_pointer (&str, g_free);

  /* values as compositions */
  ok = bolt_flags_class_from_string (klass, "default", &val, &err);
  g_assert_no_error (err);
  g_assert_true (ok);
  g_assert_cmpuint (val, ==, BOLT_KITT_DEFAULT);
  g_assert_cmpuint (val, ==, BOLT_KITT_ENABLED | BOLT_KITT_SSPM);

  ref = BOLT_KITT_ENABLED | BOLT_KITT_SSPM;
  str = bolt_flags_class_to_string (klass, ref, &err);
  g_assert_no_error (err);
  g_assert_nonnull (str);

  g_assert_true (strstr (str, "enabled"));
  g_assert_true (strstr (str, "sspm"));

  g_clear_pointer (&str, g_free);

  /* test flags updating */
  val = 0;
  chg = bolt_flags_update (0, &val, 0);
  g_assert_false (chg);

  /* no updates */
  val = 0;
  ref = BOLT_KITT_SSPM | BOLT_KITT_SKI_MODE | BOLT_KITT_TURBO_BOOST;
  chg = bolt_flags_update (ref, &val, 0);
  g_assert_false (chg);

  val = BOLT_KITT_SSPM | BOLT_KITT_SKI_MODE | BOLT_KITT_TURBO_BOOST;
  chg = bolt_flags_update (ref, &val, 0);
  g_assert_false (chg);

  chg = bolt_flags_update (ref, &val, val);
  g_assert_false (chg);

  /* finally, some updates */
  val = 0;
  ref = BOLT_KITT_SSPM | BOLT_KITT_SKI_MODE | BOLT_KITT_TURBO_BOOST;

  chg = bolt_flags_update (ref, &val, BOLT_KITT_SSPM);
  g_assert_true (chg);
  g_assert_cmpuint (val, ==, BOLT_KITT_SSPM);

  val = 0;
  chg = bolt_flags_update (ref, &val, BOLT_KITT_TURBO_BOOST);
  g_assert_true (chg);
  g_assert_cmpuint (val, ==, BOLT_KITT_TURBO_BOOST);

  val = BOLT_KITT_SSPM;
  ref = BOLT_KITT_TURBO_BOOST;
  chg = bolt_flags_update (ref, &val, BOLT_KITT_TURBO_BOOST);
  g_assert_true (chg);

  ref = BOLT_KITT_TURBO_BOOST | BOLT_KITT_SSPM;
  g_assert_cmpuint (val, ==, ref);

  val = BOLT_KITT_TURBO_BOOST | BOLT_KITT_SSPM;
  ref = 0;
  chg = bolt_flags_update (ref, &val, BOLT_KITT_TURBO_BOOST);
  g_assert_cmpuint (val, ==, BOLT_KITT_SSPM);
  g_assert_true (chg);

  /* simple checks for helper class */
  ref = BOLT_KITT_TURBO_BOOST | BOLT_KITT_SSPM;
  g_assert_true (bolt_flag_isset (ref, BOLT_KITT_TURBO_BOOST));
  g_assert_true (bolt_flag_isset (ref, BOLT_KITT_SSPM));
  g_assert_false (bolt_flag_isclear (ref,  BOLT_KITT_TURBO_BOOST));

  g_assert_false (bolt_flag_isset (ref, BOLT_KITT_SKI_MODE));
  g_assert_true (bolt_flag_isclear (ref, BOLT_KITT_SKI_MODE));
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
  g_autoptr(GError) error = NULL;
  gboolean ok;

  ok = bolt_fs_cleanup_dir (tt->path, &error);

  if (!ok && !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
    g_warning ("Could not clean up dir: %s", error->message);
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
test_io_file_write_all (TestIO *tt, gconstpointer user_data)
{
  g_autoptr(GError) error = NULL;
  g_autofree char *path = NULL;
  g_autofree char *data = NULL;
  static const char *ref = "Die Welt is alles was der Fall ist!";
  gboolean ok;
  gsize len;

  path = g_build_filename (tt->path, "file_write_all", NULL);
  ok = bolt_file_write_all (path, ref, -1, &error);

  g_assert_no_error (error);
  g_assert_true (ok);

  ok = g_file_get_contents (path, &data, &len, &error);
  g_assert_no_error (error);
  g_assert_true (ok);
  g_assert_cmpuint (strlen (ref), ==, len);
  g_assert_cmpstr (ref, ==, data);

  g_clear_pointer (&data, g_free);
  ok = bolt_file_write_all (path, ref, 5, &error);

  g_assert_no_error (error);
  g_assert_true (ok);

  ok = g_file_get_contents (path, &data, &len, &error);
  g_assert_no_error (error);
  g_assert_true (ok);
  g_assert_cmpuint (len, ==, 5);
  g_assert_true (strncmp (data, ref, 5) == 0);
}

static void
test_autoclose (TestIO *tt, gconstpointer user_data)
{
  g_autoptr(GError) err = NULL;
  g_autofree char *path = NULL;
  gboolean ok;

  {
    bolt_autoclose int fd = -1;

    fd = bolt_open ("/dev/null", O_RDONLY, 0, &err);
    g_assert_no_error (err);
    g_assert_cmpint (fd, >, -1);

    path = g_strdup_printf ("/proc/self/fd/%d", fd);
    ok = g_file_test (path, G_FILE_TEST_EXISTS);
    g_assert_true (ok);
  }

  ok = g_file_test (path, G_FILE_TEST_EXISTS);
  g_assert_false (ok);
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

#define TIME_QI "time::*"
static void
touch_and_compare (GFile *target, guint64 tp)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GFileInfo) info = NULL;
  gboolean ok;
  guint64 ts;

  ok = bolt_fs_touch (target, tp, tp + 1, &error);

  g_assert_no_error (error);
  g_assert_true (ok);

  info = g_file_query_info (target, TIME_QI, 0, NULL, &error);

  g_assert_no_error (error);
  g_assert_nonnull (info);

  ts = g_file_info_get_attribute_uint64 (info, "time::modified");
  g_assert_cmpuint (ts, ==, tp + 1);

  ts = g_file_info_get_attribute_uint64 (info, "time::access");
  g_assert_cmpuint (ts, ==, tp);
}


static void
test_fs_touch (TestIO *tt, gconstpointer user_data)
{
  g_autoptr(GFile) base = NULL;
  g_autoptr(GFile) target = NULL;
  gboolean ok;
  guint64 now;
  guint64 tp;

  base = g_file_new_for_path (tt->path);
  target = g_file_get_child (base, "this");

  ok = g_file_query_exists (target, NULL);
  g_assert_false (ok);

  now = (guint64) g_get_real_time () / G_USEC_PER_SEC;
  touch_and_compare (target, now);

  tp = 626648700;
  touch_and_compare (target, tp);
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
  g_autofree char *d1 = NULL;
  g_autofree char *d2 = NULL;
  g_autofree char *n0 = NULL;
  char buf[256] = {0, };
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

static void
test_str_set (TestRng *tt, gconstpointer user_data)
{
  g_autofree char *target = NULL;
  g_autofree char *str = NULL;

  bolt_set_str (&target, NULL);
  g_assert_null (target);

  str = g_strdup ("test");
  bolt_set_str (&target, str);

  g_assert_nonnull (target);
  g_assert_true (str == target);
  str = NULL; /* owned by target now */

  bolt_set_strdup (&target, "foobar");
  g_assert_cmpstr (target, ==, "foobar");

  bolt_set_strdup_printf (&target, "%s %s", "Hallo", "Welt");
  g_assert_cmpstr (target, ==, "Hallo Welt");
}

#define MAKE_GSTRV(...) (GStrv) (const char *[]){ __VA_ARGS__}

static void
test_strv_length (TestRng *tt, gconstpointer user_data)
{
  struct
  {
    const GStrv strv;
    gsize       l;
  } table[] = {
    {NULL, 0},
    {MAKE_GSTRV (NULL), 0},
    {MAKE_GSTRV ("a", NULL), 1},
    {MAKE_GSTRV ("a", "b", "c", "d", NULL), 4},
  };

  for (gsize i = 0; i < G_N_ELEMENTS (table); i++)
    g_assert_cmpuint (bolt_strv_length (table[i].strv), ==, table[i].l);
}

static void
test_strv_contains (TestRng *tt, gconstpointer user_data)
{

  const GStrv strv = MAKE_GSTRV ("a", "b", "c", "d", NULL);

  g_assert_false (bolt_strv_contains (NULL, "nonexistant"));
  g_assert_false (bolt_strv_contains (strv, "nonexistant"));

  for (char **iter = strv; *iter; iter++)
    {
      char **target = bolt_strv_contains (strv, *iter);
      g_assert_nonnull (target);
      g_assert_cmpuint (GPOINTER_TO_UINT ((gpointer) target), ==,
                        GPOINTER_TO_UINT ((gpointer) iter));
    }
}


typedef struct
{

  const GStrv a;
  const GStrv b;
  gboolean    result;

} StrvEqualTest;

static void
test_strv_equal (TestRng *tt, gconstpointer user_data)
{
  StrvEqualTest table[] = {
    {NULL,                        NULL,                        TRUE},
    {NULL,                        MAKE_GSTRV (NULL),           TRUE},
    {MAKE_GSTRV (NULL),           NULL,                        TRUE},
    {MAKE_GSTRV (NULL),           MAKE_GSTRV (NULL),           TRUE},
    {MAKE_GSTRV ("a", NULL),      NULL,                        FALSE},
    {MAKE_GSTRV ("a", NULL),      MAKE_GSTRV (NULL),           FALSE},
    {MAKE_GSTRV ("a", NULL),      MAKE_GSTRV ("a", NULL),      TRUE},
    {MAKE_GSTRV ("a", NULL),      MAKE_GSTRV ("b", NULL),      FALSE},
    {MAKE_GSTRV ("a", NULL),      MAKE_GSTRV ("a", NULL),      TRUE},
    {MAKE_GSTRV ("a", "b", NULL), MAKE_GSTRV ("a", NULL),      FALSE},
    {MAKE_GSTRV ("a", "b", NULL), MAKE_GSTRV ("a", "b", NULL), TRUE},
    {MAKE_GSTRV ("a", "a", NULL), MAKE_GSTRV ("a", "b", NULL), FALSE},
    {MAKE_GSTRV ("a", "a", NULL), MAKE_GSTRV ("a", "b", NULL), FALSE},
  };

  for (gsize i = 0; i < G_N_ELEMENTS (table); i++)
    {
      StrvEqualTest *t = &table[i];
      gboolean res = bolt_strv_equal (t->a, t->b);

      g_debug ("strv-equal[%2" G_GSIZE_FORMAT "] expected | got: %3s | %3s",
               i, bolt_yesno (res), bolt_yesno (res));
      g_assert_true (res == t->result);
    }
}

typedef struct
{

  const GStrv before;
  const GStrv after;
  gboolean    result;
  const GStrv added;
  const GStrv removed;
} StrvDiffTest;

#define MK_STRV0(...) (GStrv) (const char *[]){ __VA_ARGS__, NULL}

static void
test_strv_diff (TestRng *tt, gconstpointer user_data)
{
  StrvDiffTest table[] = {
    {NULL,                      NULL,                     FALSE, NULL,                NULL               },
    {MK_STRV0 ("a"),            MK_STRV0 ("a"),           FALSE, NULL,                NULL               },
    {MK_STRV0 ("a", "b"),       MK_STRV0 ("a", "b"),      FALSE, NULL,                NULL               },
    {NULL,                      MK_STRV0 ("a"),           TRUE,  MK_STRV0 ("a"),      NULL               },
    {NULL,                      MK_STRV0 ("a", "b"),      TRUE,  MK_STRV0 ("a", "b"), NULL               },
    {MK_STRV0 ("a"),            NULL,                     TRUE,  NULL,                MK_STRV0 ("a")     },
    {MK_STRV0 ("a", "b"),       NULL,                     TRUE,  NULL,                MK_STRV0 ("a", "b")},
    {MK_STRV0 ("a", "b", "d"),  MK_STRV0 ("a", "c", "d"), TRUE,  MK_STRV0 ("c"),      MK_STRV0 ("b")     },
    {MK_STRV0 ("a", "b", "x"),  MK_STRV0 ("x", "c", "d"), TRUE,  MK_STRV0 ("c", "d"), MK_STRV0 ("a", "b")},
    {MK_STRV0 ("b", "x", "a"),  MK_STRV0 ("d", "x", "c"), TRUE,  MK_STRV0 ("c", "d"), MK_STRV0 ("a", "b")},
  };

  for (gsize i = 0; i < G_N_ELEMENTS (table); i++)
    {
      g_autoptr(GHashTable) diff = NULL;
      g_autoptr(GPtrArray) add = NULL;
      g_autoptr(GPtrArray) del = NULL;
      GStrv added = NULL;
      GStrv removed = NULL;
      StrvDiffTest *t = &table[i];
      GHashTableIter hi;
      gpointer k, v;
      gboolean add_equal;
      gboolean rem_equal;
      gboolean res;

      diff = bolt_strv_diff (t->before, t->after);

      add = g_ptr_array_new ();
      del = g_ptr_array_new ();

      g_hash_table_iter_init (&hi, diff);
      while (g_hash_table_iter_next (&hi, &k, &v))
        {
          gint op = GPOINTER_TO_INT (v);
          if (op == '+')
            g_ptr_array_add (add, k);
          else if (op == '-')
            g_ptr_array_add (del, k);
          else
            g_warning ("unknown op: %c", op);
        }

      g_ptr_array_sort (add, bolt_comparefn_strcmp);
      g_ptr_array_sort (del, bolt_comparefn_strcmp);

      g_ptr_array_add (add, NULL);
      g_ptr_array_add (del, NULL);

      added = (GStrv) add->pdata;
      removed = (GStrv) del->pdata;

      res = g_hash_table_size (diff) > 0;
      add_equal = bolt_strv_equal (added, t->added);
      rem_equal = bolt_strv_equal (removed, t->removed);

      g_debug ("strv-diff[%2" G_GSIZE_FORMAT "] expected, got | %3s, %3s add: %s, rem: %s",
               i, bolt_yesno (t->result), bolt_yesno (res), bolt_yesno (add_equal), bolt_yesno (rem_equal));

      g_assert_true (res == t->result);
      bolt_assert_strv_equal (added, t->added, -1);
      bolt_assert_strv_equal (removed, t->removed, -1);
    }
}

static void
test_strv_permute (TestRng *tt, gconstpointer user_data)
{
  g_auto(GStrv) tst = NULL;
  const char *ref[] = {"a", "b", "c", "d", NULL};
  char *empty[] = {NULL};

  bolt_strv_permute (NULL);
  bolt_strv_permute (empty);

  g_assert_cmpuint (bolt_strv_length (empty), ==, 0U);

  tst = g_strdupv ((char **) ref);
  bolt_strv_permute (tst);
  g_assert_false (bolt_strv_equal ((char **) ref, (char **) tst));
}

static void
test_strv_rotate_left (TestRng *tt, gconstpointer user_data)
{
  g_auto(GStrv) a = NULL;
  char **target;

  /* handle NULL */
  target = bolt_strv_rotate_left (NULL);
  g_assert_null (target);

  /* single element */
  a = g_strsplit ("a", ":", -1);
  g_assert_cmpuint (g_strv_length (a), ==, 1);
  g_assert_cmpstr (a[0], ==, "a");

  target = bolt_strv_rotate_left (a);
  g_assert_cmpuint (g_strv_length (a), ==, 1);
  g_assert_cmpstr (a[0], ==, "a");
  g_assert_true (target == &a[0]);

  /* two elements */
  g_strfreev (a);
  a = g_strsplit ("a:b", ":", -1);

  g_assert_cmpuint (g_strv_length (a), ==, 2);
  g_assert_cmpstr (a[0], ==, "a");
  g_assert_cmpstr (a[1], ==, "b");

  target = bolt_strv_rotate_left (a);
  g_assert_cmpuint (g_strv_length (a), ==, 2);
  g_assert_cmpstr (a[1], ==, "a");
  g_assert_cmpstr (a[0], ==, "b");
  g_assert_true (target == &a[1]);

  /* now > 2 */
  g_strfreev (a);
  a = g_strsplit ("a:b:c:d:e", ":", -1);

  g_assert_cmpuint (g_strv_length (a), ==, 5);
  g_assert_cmpstr (a[0], ==, "a");
  g_assert_cmpstr (a[4], ==, "e");

  target = bolt_strv_rotate_left (a);
  g_assert_cmpuint (g_strv_length (a), ==, 5);
  g_assert_cmpstr (a[0], ==, "b");
  g_assert_cmpstr (a[1], ==, "c");
  g_assert_cmpstr (a[2], ==, "d");
  g_assert_cmpstr (a[3], ==, "e");
  g_assert_cmpstr (a[4], ==, "a");
  g_assert_true (target == &a[4]);

  /* now with empty strings in between */
  g_strfreev (a);
  a = g_strsplit ("a:::d:e", ":", -1);
  g_assert_cmpuint (g_strv_length (a), ==, 5);
  g_assert_cmpstr (a[0], ==, "a");
  g_assert_cmpstr (a[3], ==, "d");
  g_assert_cmpstr (a[4], ==, "e");

  target = bolt_strv_rotate_left (a);
  g_assert_cmpstr (a[0], ==, "");
  g_assert_cmpstr (a[1], ==, "");
  g_assert_cmpstr (a[2], ==, "d");
  g_assert_cmpstr (a[3], ==, "e");
  g_assert_cmpstr (a[4], ==, "a");
  g_assert_true (target == &a[4]);
}

static void
test_list_nh (TestRng *tt, gconstpointer user_data)
{
  BoltList n[10];
  BoltList *l = n;
  BoltList *k;
  BoltList iter;
  guint c;

  bolt_list_init (l);
  g_assert_cmpuint (bolt_nhlist_len (NULL), ==, 0);
  g_assert_cmpuint (bolt_nhlist_len (l), ==, 1);

  g_assert_true (l->next == l);
  g_assert_true (l->prev == l);

  c = 0;
  bolt_nhlist_iter_init (&iter, l);
  while ((k = bolt_nhlist_iter_next (&iter)))
    {
      BoltList *p = bolt_nhlist_iter_node (&iter);
      g_assert_true (k == l);
      g_assert_true (p == l);
      c++;
    }
  g_assert_cmpuint (c, ==, 1);

  for (gsize i = 1; i < 10; i++)
    {
      bolt_list_init (&n[i]);
      bolt_list_add_before (l, &n[i]);
      g_assert_cmpuint (bolt_nhlist_len (l), ==, i + 1);
    }

  for (gsize i = 0; i < 10; i++)
    {
      gsize j = (i + 1) % 10;
      g_assert_true (l[i].next == &l[j]);
      g_assert_true (l[j].prev == &l[i]);

      g_assert_true (l[i].next->prev == &l[i]);
      g_assert_true (l[i].prev->next == &l[i]);
    }

  c = 0;
  bolt_nhlist_iter_init (&iter, l);
  while ((k = bolt_nhlist_iter_next (&iter)))
    {
      BoltList *p = bolt_nhlist_iter_node (&iter);
      g_assert_true (k == n + (c % 10));
      g_assert_true (k == p);
      c++;
    }

  g_assert_cmpuint (c, ==, 10);
}

static void
test_steal (TestRng *tt, gconstpointer user_data)
{
  unsigned int arr[] = {0, 1, 2};
  unsigned int uit;
  char c = ' ';
  char *ptr = &c;
  char *ref;
  int ifd = 42;
  int chk;

  uit = bolt_steal (arr + 1, 0);
  g_assert_cmpuint (uit, ==, 1);
  g_assert_cmpuint (arr[1], ==, 0);

  ref = bolt_steal (&ptr, NULL);
  g_assert_true (ptr == NULL);
  g_assert_true (ref == &c);

  chk = bolt_steal (&ifd, -1);
  g_assert_cmpint (chk, ==, 42);
  g_assert_cmpint (ifd, ==, -1);
}

static void
test_swap (TestRng *tt, gconstpointer user_data)
{
  int ia = 0, ib = 1;
  int *pa = &ia, *pb = &ib;

  g_assert_cmpint (ia, ==, 0);
  g_assert_cmpint (ib, ==, 1);

  bolt_swap (ia, ib);

  g_assert_cmpint (ia, ==, 1);
  g_assert_cmpint (ib, ==, 0);

  bolt_swap (pa, pb);
  g_assert_true (pa == &ib);
  g_assert_true (pb == &ia);

  bolt_swap (pa, pa);
  g_assert_true (pa == &ib);
  g_assert_cmpint (*pa, ==, ib);
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

  g_test_add ("/common/error",
              TestRng,
              NULL,
              NULL,
              test_error,
              NULL);

  g_test_add ("/common/flags",
              TestRng,
              NULL,
              NULL,
              test_flags,
              NULL);

  g_test_add ("/common/rng",
              TestRng,
              NULL,
              NULL,
              test_rng,
              NULL);

  g_test_add ("/common/io/verify",
              TestIO,
              NULL,
              test_io_setup,
              test_io_verify,
              test_io_tear_down);

  g_test_add ("/common/io/file_write_all",
              TestIO,
              NULL,
              test_io_setup,
              test_io_file_write_all,
              test_io_tear_down);

  g_test_add ("/common/io/autoclose",
              TestIO,
              NULL,
              NULL,
              test_autoclose,
              NULL);

  g_test_add ("/common/fs",
              TestIO,
              NULL,
              test_io_setup,
              test_fs,
              test_io_tear_down);

  g_test_add ("/common/fs/touch",
              TestIO,
              NULL,
              test_io_setup,
              test_fs_touch,
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

  g_test_add ("/common/str/set",
              TestRng,
              NULL,
              NULL,
              test_str_set,
              NULL);

  g_test_add ("/common/strv/equal",
              TestRng,
              NULL,
              NULL,
              test_strv_equal,
              NULL);

  g_test_add ("/common/strv/length",
              TestRng,
              NULL,
              NULL,
              test_strv_length,
              NULL);

  g_test_add ("/common/strv/contains",
              TestRng,
              NULL,
              NULL,
              test_strv_contains,
              NULL);

  g_test_add ("/common/strv/diff",
              TestRng,
              NULL,
              NULL,
              test_strv_diff,
              NULL);

  g_test_add ("/common/strv/permute",
              TestRng,
              NULL,
              NULL,
              test_strv_permute,
              NULL);

  g_test_add ("/common/strv/rotate_left",
              TestRng,
              NULL,
              NULL,
              test_strv_rotate_left,
              NULL);

  g_test_add ("/common/list/nh",
              TestRng,
              NULL,
              NULL,
              test_list_nh,
              NULL);

  g_test_add ("/common/macro/steal",
              TestRng,
              NULL,
              NULL,
              test_steal,
              NULL);

  g_test_add ("/common/macro/swap",
              TestRng,
              NULL,
              NULL,
              test_swap,
              NULL);

  return g_test_run ();
}

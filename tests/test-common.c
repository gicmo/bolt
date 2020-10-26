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

#include "bolt-dbus.h"
#include "bolt-enums.h"
#include "bolt-error.h"
#include "bolt-fs.h"
#include "bolt-io.h"
#include "bolt-list.h"
#include "bolt-rnd.h"
#include "bolt-str.h"
#include "bolt-term.h"
#include "bolt-test.h"
#include "bolt-time.h"
#include "bolt-unix.h"
#include "mock-sysfs.h"

#include "test-enums.h"
#include "bolt-test-resources.h"

#include <glib.h>
#include <gio/gio.h>
#include <glib/gprintf.h>

#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h> /* unlinkat, fork */

#if !GLIB_CHECK_VERSION (2, 57, 0)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GEnumClass, g_type_class_unref);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GFlagsClass, g_type_class_unref);
#endif
typedef struct
{
  int dummy;
} TestDummy;

#define TEST_DBUS_GRESOURCE_PATH "/bolt/tests/exported/example.bolt.xml"
#define TEST_DBUS_INTERFACE "org.gnome.bolt.Example"

static void
test_dbus_interface_info_find (TestDummy *tt, gconstpointer user_data)
{
  g_autoptr(GBytes) data = NULL;
  g_autoptr(GError) error = NULL;
  GDBusInterfaceInfo *info = NULL;
  const char *xml;

  data = g_resources_lookup_data (TEST_DBUS_GRESOURCE_PATH,
                                  G_RESOURCE_LOOKUP_FLAGS_NONE,
                                  &error);
  g_assert_no_error (error);
  g_assert_nonnull (data);

  xml = g_bytes_get_data (data, NULL);

  info = bolt_dbus_interface_info_find (xml, "NON-EXISTENT", &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_assert_null (info);
  g_clear_error (&error);

  info = bolt_dbus_interface_info_find (xml, TEST_DBUS_INTERFACE, &error);
  g_assert_no_error (error);
  g_assert_nonnull (info);
  g_dbus_interface_info_unref (info);
}

static void
test_dbus_interface_info_lookup (TestDummy *tt, gconstpointer user_data)
{
  g_autoptr(GError) error = NULL;
  GDBusInterfaceInfo *info = NULL;

  info = bolt_dbus_interface_info_lookup ("NON-EXISTENT",
                                          "NON-EXISTENT",
                                          &error);
  g_assert_error (error, G_RESOURCE_ERROR, G_RESOURCE_ERROR_NOT_FOUND);
  g_assert_null (info);
  g_clear_error (&error);

  info = bolt_dbus_interface_info_lookup (TEST_DBUS_GRESOURCE_PATH,
                                          "NON-EXISTENT",
                                          &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_assert_null (info);
  g_clear_error (&error);

  info = bolt_dbus_interface_info_lookup (TEST_DBUS_GRESOURCE_PATH,
                                          TEST_DBUS_INTERFACE,
                                          &error);
  g_assert_no_error (error);
  g_assert_nonnull (info);
  g_dbus_interface_info_unref (info);
}

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
    {BOLT_TYPE_SECURITY,  "none",    BOLT_SECURITY_NONE},
    {BOLT_TYPE_SECURITY,  "dponly",  BOLT_SECURITY_DPONLY},
    {BOLT_TYPE_SECURITY,  "user",    BOLT_SECURITY_USER},
    {BOLT_TYPE_SECURITY,  "secure",  BOLT_SECURITY_SECURE},
    {BOLT_TYPE_TEST_ENUM, "unknown", BOLT_TEST_UNKNOWN},
    {BOLT_TYPE_TEST_ENUM, "one",     BOLT_TEST_ONE},
    {BOLT_TYPE_TEST_ENUM, "two",     BOLT_TEST_TWO},
    {BOLT_TYPE_TEST_ENUM, "three",   BOLT_TEST_THREE},
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
  g_autoptr(GError) error = NULL;
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
  g_assert_false (ok);
  g_assert_false (g_dbus_error_is_remote_error (target));
  g_assert_cmpstr (target->message, ==, buserr->message);

  /* bolt_error_for_errno */
  ok = bolt_error_for_errno (NULL, 0, "no error!");
  g_assert_true (ok);

  ok = bolt_error_for_errno (&error, 0, "no error!");
  g_assert_no_error (error);
  g_assert_true (ok);

  ok = bolt_error_for_errno (NULL, ENOENT, "no such thing");
  g_assert_false (ok);

  ok = bolt_error_for_errno (&error, ENOENT, "no such thing");
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_assert_true (g_str_has_prefix (error->message, "no such thing"));
  g_assert_false (ok);
  g_clear_pointer (&error, g_error_free);

  ok = bolt_error_for_errno (&error, ENOENT, "%m");
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_debug ("ENOENT formatted via %%m is '%s'", error->message);
  g_assert_true (strlen (error->message) > 0);
  g_assert_false (ok);
  g_clear_pointer (&error, g_error_free);
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

  /* handle invalid values */
  klass = g_type_class_ref (BOLT_TYPE_KITT_FLAGS);
  g_assert_nonnull (klass);

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

  str = bolt_flags_class_to_string (klass, BOLT_KITT_SKI_MODE << 1, &err);
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

  g_clear_pointer (&tt->path, g_free);
}


static const char *valid_uid = "f96b4cc77f196068ec454cb6006514c602d1011f47dd275cf5c6b8a47744f049";

static void
test_io_errors (TestIO *tt, gconstpointer user_data)
{
  g_autoptr(GError) err = NULL;
  g_autoptr(GFile) empty_file = NULL;
  g_autoptr(DIR) root = NULL;
  g_autoptr(DIR) dp = NULL;
  g_autofree char *noexist = NULL;
  g_autofree char *subdir = NULL;
  g_autofree char *rdonly = NULL;
  g_autofree char *empty = NULL;
  g_autofree char *fifo = NULL;
  g_autofree char *data = NULL;
  bolt_autoclose int to = -1;
  bolt_autoclose int from = -1;
  struct stat st;
  char buffer[256] = {0, };
  gboolean ok;
  int fd = -1;
  int iv;
  int r;

  /* preparation  */
  root = bolt_opendir (tt->path, &err);
  g_assert_no_error (err);
  g_assert_nonnull (root);

  noexist = g_build_filename (tt->path, "NONEXISTENT", NULL);
  subdir = g_build_filename (tt->path, "subdir", NULL);

  rdonly = g_build_filename (tt->path, "readonly", NULL);
  ok = g_file_set_contents (rdonly, "Hallo Welt", -1, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  r = chmod (rdonly, 0400);
  g_assert_cmpint (r, >, -1);

  empty = g_build_filename (tt->path, "empty", NULL);
  empty_file = g_file_new_for_path (empty);
  ok = bolt_fs_touch (empty_file, 0, 0, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  /* error handling*/
  fd = bolt_open (noexist, O_RDONLY | O_CLOEXEC | O_NOCTTY, 0, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_assert_cmpint (fd, <, 0);
  g_clear_pointer (&err, g_error_free);

  ok = bolt_close (fd, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_FAILED);
  g_assert_false (ok);
  g_clear_pointer (&err, g_error_free);

  /* create a pipe, so we can have some read and write errors
   * by using the wrong end */
  fifo = g_build_filename (tt->path, "fifo", NULL);
  r = bolt_mkfifo (fifo, 0600, &err);
  g_assert_no_error (err);
  g_assert_cmpint (r, ==, 0);

  /*     reader */
  from = bolt_open (fifo, O_RDONLY | O_CLOEXEC | O_NONBLOCK, 0, &err);
  g_assert_no_error (err);
  g_assert_cmpint (from, >, -1);

  /*     writer */
  to = bolt_open (fifo, O_WRONLY | O_CLOEXEC | O_NONBLOCK, 0, &err);
  g_assert_no_error (err);
  g_assert_cmpint (to, >, -1);

  ok = bolt_read_all (to, buffer, sizeof (buffer), NULL, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_FAILED);
  g_assert_false (ok);
  g_clear_pointer (&err, g_error_free);

  ok = bolt_write_all (from, buffer, sizeof (buffer), &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_FAILED);
  g_assert_false (ok);
  g_clear_pointer (&err, g_error_free);

  /* ftruncate */
  ok = bolt_ftruncate (to, 0, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  g_assert_false (ok);
  g_clear_pointer (&err, g_error_free);

  /* opendir error checking */
  dp = bolt_opendir (noexist, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_assert_null (dp);
  g_clear_pointer (&err, g_error_free);

  /* opendir_at error checking */
  dp = bolt_opendir_at (dirfd (root), "NONEXISTENT", 0, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_assert_null (dp);
  g_clear_pointer (&err, g_error_free);

  dp = bolt_opendir_at (dirfd (root), "fifo", 0, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY);
  g_assert_null (dp);
  g_clear_pointer (&err, g_error_free);

  /* closedir */
#ifdef __GLIBC__
  ok = bolt_closedir (NULL, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  g_assert_false (ok);
  g_clear_pointer (&err, g_error_free);
#endif

  /* rmdir */
  ok = bolt_rmdir (noexist, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_assert_false (ok);
  g_clear_pointer (&err, g_error_free);

  /* openat */
  fd = bolt_openat (dirfd (root), "NONEXISTENT", 0, 0, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_assert_cmpint (fd, <, 0);
  g_clear_pointer (&err, g_error_free);

  /* unlink error checking  */
  ok = bolt_unlink (noexist, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_assert_false (ok);
  g_clear_pointer (&err, g_error_free);

  /* unlink_at */
  ok = bolt_unlink_at (dirfd (root), "NONEXISTENT", 0, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_assert_false (ok);
  g_clear_pointer (&err, g_error_free);

  /* read_value_at */
  data = bolt_read_value_at (dirfd (root), "NONEXISTENT", &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_assert_null (data);
  g_clear_pointer (&err, g_error_free);

  data = bolt_read_value_at (dirfd (root), "empty", &err);
  g_assert_no_error (err);
  g_assert_cmpstr (data, ==, "");

  /* write char at */
  ok = bolt_write_char_at (dirfd (root), "NONEXISTENT", 'c', &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_assert_false (ok);
  g_clear_pointer (&err, g_error_free);

  if (geteuid () != 0)
    {
      /* if we run as root, maybe inside a container, we will
       *  be able o do that anyway so skip it in that case */
      ok = bolt_write_char_at (dirfd (root), "readonly", 'c', &err);
      g_assert_error (err, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
      g_assert_false (ok);
      g_clear_pointer (&err, g_error_free);
    }

  /* read_int_at */
  ok = bolt_read_int_at (dirfd (root), "NONEXISTENT", &iv, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_assert_false (ok);
  g_clear_pointer (&err, g_error_free);

  ok = bolt_read_int_at (dirfd (root), "readonly", &iv, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  g_assert_false (ok);
  g_clear_pointer (&err, g_error_free);

  /* pipe error checking */
  fd = bolt_mkfifo (tt->path, 0600, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_EXISTS);
  g_assert_cmpint (fd, <, 0);
  g_clear_pointer (&err, g_error_free);

  /* faddflags */
  ok = bolt_faddflags (-1, 0, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_FAILED);
  g_assert_false (ok);
  g_clear_pointer (&err, g_error_free);

  /* fstat */
  ok = bolt_fstat (-1, &st, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_FAILED);
  g_assert_false (ok);
  g_clear_pointer (&err, g_error_free);

  /* fstatat */
  ok = bolt_fstatat (dirfd (root), "NONEXISTENT", &st, 0, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_assert_false (ok);
  g_clear_pointer (&err, g_error_free);

  /* fdatasync */
  ok = bolt_fdatasync (-1, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_FAILED);
  g_assert_false (ok);
  g_clear_pointer (&err, g_error_free);

  /* lseek */
  ok = bolt_lseek (to, 0, SEEK_SET, NULL, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_FAILED);
  g_assert_false (ok);
  g_clear_pointer (&err, g_error_free);

  /* rename */
  ok = bolt_rename (noexist, subdir, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND);
  g_assert_false (ok);
  g_clear_pointer (&err, g_error_free);

  /* copy_bytes */
  ok = bolt_copy_bytes (to, from, 1, &err);
  g_assert_nonnull (err);
  g_assert_cmpint (err->domain, ==, G_IO_ERROR);
  g_assert_false (ok);
  g_clear_pointer (&err, g_error_free);
}

static void
test_io_verify (TestIO *tt, gconstpointer user_data)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(DIR) d = NULL;
  g_autofree char *uid_path = NULL;
  gboolean ok;

  /* more preparation*/
  d = bolt_opendir (tt->path, &error);

  g_assert_nonnull (d);
  g_assert_no_error (error);

  /* unique_id missing */
  ok = bolt_verify_uid (dirfd (d), valid_uid, &error);
  g_assert_false (ok);
  g_assert_error (error, BOLT_ERROR, BOLT_ERROR_FAILED);
  g_clear_error (&error);

  /* existing, but wrong value */
  uid_path = g_build_filename (tt->path, "unique_id", NULL);

  ok = g_file_set_contents (uid_path,
                            "wrong_to_small",
                            -1,
                            &error);
  g_assert_true (ok);
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
test_io_write_file_at (TestIO *tt, gconstpointer user_data)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(DIR) dir = NULL;
  g_autofree char *path = NULL;
  g_autofree char *data = NULL;
  static const char *ref = "The world is everything that is the case.";
  gboolean ok;
  gsize len;

  dir = bolt_opendir (tt->path, &error);
  g_assert_no_error (error);
  g_assert_nonnull (dir);

  ok = bolt_write_file_at (dirfd (dir), "test.txt", ref, -1, &error);

  g_assert_no_error (error);
  g_assert_true (ok);

  path = g_build_filename (tt->path, "test.txt", NULL);
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
test_io_file_write_all (TestIO *tt, gconstpointer user_data)
{
  g_autoptr(GError) error = NULL;
  g_autofree char *path = NULL;
  g_autofree char *data = NULL;
  static const char *ref = "The world is everything that is the case.";
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
test_io_copy_bytes (TestIO *tt, gconstpointer user_data)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GChecksum) chk = NULL;
  g_autofree char *source = NULL;
  g_autofree char *target = NULL;
  g_autofree char *chksum = NULL;
  const gsize N = 1024;
  char buf[4096];
  gboolean ok;
  int from = -1;
  int to = -1;

  /* bolt_copy_bytes uses copy_file_range(2) internally, which
   * was added in linux 4.5. */
  skip_test_unless (bolt_check_kernel_version (4, 5) || g_test_thorough (),
                    "linux kernel < 4.5, copy_file_range syscall missing");

  chk = g_checksum_new (G_CHECKSUM_SHA256);

  source = g_build_filename (tt->path, "copy_bytes_source", NULL);
  from = bolt_open (source, O_RDWR | O_CREAT, 0666, &error);
  g_assert_no_error (error);
  g_assert_cmpint (from, >, -1);

  for (gsize i = 0; i < N; i++)
    {
      bolt_random_prng (buf, sizeof (buf));
      ok = bolt_write_all (from, buf, sizeof (buf), &error);
      g_assert_no_error (error);
      g_assert_true (ok);
      g_checksum_update (chk, (const guchar *) buf, sizeof (buf));
    }

  chksum = g_strdup (g_checksum_get_string (chk));

  /* close, reopen readonly */
  ok = bolt_close (from, &error);
  g_assert_no_error (error);
  g_assert_true (ok);

  from = bolt_open (source, O_CLOEXEC | O_RDONLY, 0, &error);
  g_assert_no_error (error);
  g_assert_cmpint (from, >, -1);


  /* copy the data with bolt_copy_bytes */
  target = g_build_filename (tt->path, "copy_bytes_target", NULL);
  to = bolt_open (target, O_RDWR | O_CREAT, 0666, &error);
  g_assert_no_error (error);
  g_assert_cmpint (to, >, -1);

  ok = bolt_copy_bytes (from, to, N * sizeof (buf), &error);
  g_assert_no_error (error);
  g_assert_true (ok);

  ok = bolt_close (from, &error);
  g_assert_no_error (error);
  g_assert_true (ok);

  /* close, reopen, check checksum */
  ok = bolt_close (to, &error);
  g_assert_no_error (error);
  g_assert_true (ok);

  to = bolt_open (target, O_CLOEXEC | O_RDONLY, 0, &error);
  g_assert_no_error (error);
  g_assert_cmpint (to, >, -1);

  g_checksum_reset (chk);
  for (gsize i = 0; i < N; i++)
    {
      gsize nread = 0;

      bolt_read_all (to, buf, sizeof (buf), &nread, &error);
      g_assert_no_error (error);
      g_checksum_update (chk, (const guchar *) buf, nread);
    }

  ok = bolt_close (to, &error);
  g_assert_no_error (error);
  g_assert_true (ok);

  g_assert_cmpstr (g_checksum_get_string (chk),
                   ==,
                   chksum);
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
  g_autoptr(GFile) other = NULL;
  g_autoptr(GError) error = NULL;
  g_autofree char *path = NULL;
  struct stat st;
  gboolean ok;
  int r;

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

  if (geteuid () == 0)
    /* if we run as root, maybe inside a container, we will
     * be able to do that anyway so skip it in that case */
    return;

  /* check error checking */
  r = stat (tt->path, &st);
  g_assert_cmpint (r, >, -1);

  /*     make dir read only */
  r = chmod (tt->path, st.st_mode & (~00222));
  g_assert_cmpint (r, >, -1);

  other = g_file_get_child (base, "this/and/that");
  ok = bolt_fs_make_parent_dirs (other, &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED);
  g_assert_false (ok);

  /*     and back */
  r = chmod (tt->path, st.st_mode);
  g_assert_cmpint (r, >, -1);
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
  g_autoptr(GFileInfo) info = NULL;
  g_autoptr(GError) err = NULL;
  g_autoptr(GFile) base = NULL;
  g_autoptr(GFile) target = NULL;
  gboolean ok;
  guint64 now;
  guint64 tp;
  guint64 ts;

  base = g_file_new_for_path (tt->path);
  target = g_file_get_child (base, "this");

  ok = g_file_query_exists (target, NULL);
  g_assert_false (ok);

  now = (guint64) g_get_real_time () / G_USEC_PER_SEC;
  touch_and_compare (target, now);

  tp = 626648700;
  touch_and_compare (target, tp);

  /* omit one of them, start with atime */
  ok = bolt_fs_touch (target, 0, 42, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  info = g_file_query_info (target, TIME_QI, 0, NULL, &err);

  g_assert_no_error (err);
  g_assert_nonnull (info);

  ts = g_file_info_get_attribute_uint64 (info, "time::access");
  g_assert_cmpuint (ts, ==, tp);

  ts = g_file_info_get_attribute_uint64 (info, "time::modified");
  g_assert_cmpuint (ts, ==, 42);

  /* omit mtime */
  ok = bolt_fs_touch (target, 42, 0, &err);
  g_assert_no_error (err);
  g_assert_true (ok);

  g_clear_object (&info);
  info = g_file_query_info (target, TIME_QI, 0, NULL, &err);

  g_assert_no_error (err);
  g_assert_nonnull (info);

  ts = g_file_info_get_attribute_uint64 (info, "time::access");
  g_assert_cmpuint (ts, ==, 42);

  /* mtime 0 means ignore, effectively meaning now, for touch */
  ts = g_file_info_get_attribute_uint64 (info, "time::modified");
  g_assert_cmpuint (ts, >=, now);
  now = (guint64) g_get_real_time () / G_USEC_PER_SEC;
  g_assert_cmpuint (ts, <=, now);
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
  buf[0] = 'b'; /* make sure we never have an empty string */
  buf[1] = 'o';
  buf[2] = 'l';
  buf[3] = 'l';

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
test_str_parse_int (TestRng *tt, gconstpointer user_data)
{
  struct
  {
    const char *str;
    gint        val;
    gboolean    error;
  } table[] = {
    {"0",                                        0,                FALSE},
    {"1",                                        1,                FALSE},
    {"-1",                                      -1,                FALSE},
#if __SIZEOF_INT__ == 4
    {"2147483647",                      2147483647,                FALSE}, /* MAX_INT */
    {"-2147483648",                    -2147483648,                FALSE}, /* MIN_INT */
    {"2147483648",                               0,                TRUE},  /* MAX_INT + 1 */
    {"-2147483649",                              0,                TRUE},  /* MIN_INT - 1 */
#elif __SIZEOF_INT__ == 8
    {"9223372036854775807",    9223372036854775807,                FALSE}, /* MAX_INT */
    {"-9223372036854775808",  -9223372036854775808,                FALSE}, /* MIN_INT */
    {"9223372036854775808",                      0,                TRUE},  /* MAX_INT + 1 */
    {"-9223372036854775809",                     0,                TRUE},  /* MIN_INT - 1 */
#else
    #warning __SIZEOF_INT__ not handled
#endif
    {"notanint",                                 0,                TRUE},
    {"9223372036854775808",                      0,                TRUE}, /* overflow */
    {"-9223372036854775809",                     0,                TRUE}, /* underflow */
  };

  for (gsize i = 0; i < G_N_ELEMENTS (table); i++)
    {
      g_autoptr(GError) error = NULL;
      gboolean ok;
      gint v;

      errno = 0;
      ok = bolt_str_parse_as_int (table[i].str, &v, &error);

      if (table[i].error)
        {
          int err = errno;

          g_assert_nonnull (error);
          g_assert_false (ok);
          g_assert_cmpint (err, !=, 0);
        }
      else
        {
          g_assert_cmpint (table[i].val, ==, v);
          g_assert_true (ok);
        }
    }
}

static void
test_str_parse_uint (TestRng *tt, gconstpointer user_data)
{
  struct
  {
    const char *str;
    guint       val;
    gboolean    error;
  } table[] = {
    {"0",                                        0,                FALSE},
    {"1",                                        1,                FALSE},
    {"-1",                                       0,                TRUE}, /* negative */
#if __SIZEOF_INT__ == 4
    {"4294967295",                      4294967295,                FALSE}, /* MAX_UINT */
    {"4294967296",                               0,                TRUE},  /* MAX_UINT + 1 */
#elif __SIZEOF_INT__ == 8
    {"18446744073709551615",  18446744073709551615,                FALSE}, /* MAX_INT */
    {"18446744073709551616",                     0,                TRUE},  /* MAX_INT + 1 */
#else
    #warning __SIZEOF_INT__ not handled
#endif
    {"notanint",                                 0,                TRUE},
    {"18446744073709551617",                     0,                TRUE}, /* overflow */
  };

  for (gsize i = 0; i < G_N_ELEMENTS (table); i++)
    {
      g_autoptr(GError) error = NULL;
      gboolean ok;
      guint v;

      errno = 0;
      ok = bolt_str_parse_as_uint (table[i].str, &v, &error);

      if (g_test_verbose ())
        g_test_message ("parsing '%s', expecting: %s", table[i].str,
                        (table[i].error ? "error" : "success"));

      if (table[i].error)
        {
          int err = errno;

          if (g_test_verbose ())
            g_assert_nonnull (error);
          g_assert_false (ok);
          g_assert_cmpint (err, !=, 0);
        }
      else
        {
          g_assert_cmpuint (table[i].val, ==, v);
          g_assert_true (ok);
        }
    }
}

static void
test_str_parse_uint64 (TestRng *tt, gconstpointer user_data)
{
  struct
  {
    const char *str;
    guint64     val;
    gboolean    error;
  } table[] = {
    {"0",                    0,                  FALSE},
    {"1",                    1,                  FALSE},
    {"0xffffffffffffffff",   0xffffffffffffffff, FALSE}, /* G_MAXUINT64 */
    {"notauint64",           0,                  TRUE},
    {"18446744073709551616", 0,                  TRUE}, /* overflow (G_MAXUINT64 + 1) */
  };

  for (gsize i = 0; i < G_N_ELEMENTS (table); i++)
    {
      g_autoptr(GError) err = NULL;
      gboolean ok;
      guint64 v;

      ok = bolt_str_parse_as_uint64 (table[i].str, &v, &err);
      if (table[i].error)
        {
          int e = errno;

          g_assert_nonnull (err);
          g_assert_false (ok);
          g_assert_cmpint (e, !=, 0);
        }
      else
        {
          g_assert_cmpuint (table[i].val, ==, v);
          g_assert_true (ok);
        }
    }
}

static void
test_str_parse_uint32 (TestRng *tt, gconstpointer user_data)
{
  struct
  {
    const char *str;
    guint32     val;
    gboolean    error;
  } table[] = {
    {"0",                    0,          FALSE},
    {"1",                    1,          FALSE},
    {"0xffffffff",           0xffffffff, FALSE}, /* G_MAXUINT64 */
    {"notauint64",           0,          TRUE},
    {"4294967296",           0,          TRUE}, /* overflow (G_MAXUINT32 + 1) */
  };

  for (gsize i = 0; i < G_N_ELEMENTS (table); i++)
    {
      g_autoptr(GError) err = NULL;
      gboolean ok;
      guint32 v;

      ok = bolt_str_parse_as_uint32 (table[i].str, &v, &err);
      if (table[i].error)
        {
          int e = errno;

          g_assert_nonnull (err);
          g_assert_false (ok);
          g_assert_cmpint (e, !=, 0);
        }
      else
        {
          g_assert_cmpuint (table[i].val, ==, v);
          g_assert_true (ok);
        }
    }
}

static void
test_str_parse_boolean (TestRng *tt, gconstpointer user_data)
{
  struct
  {
    const char *str;
    gboolean    val;
    gboolean    error;
  } table[] = {
    {"TRUE",      TRUE,  FALSE},
    {"YES",       TRUE,  FALSE},
    {"1",         TRUE,  FALSE},
    {"FALSE",     FALSE, FALSE},
    {"no",        FALSE, FALSE},
    {"0",         FALSE, FALSE},
    {"notabool",  FALSE, TRUE},
    {"12",        FALSE, TRUE},
  };

  for (gsize i = 0; i < G_N_ELEMENTS (table); i++)
    {
      g_autoptr(GError) err = NULL;
      gboolean ok;
      gboolean v;

      ok = bolt_str_parse_as_boolean (table[i].str, &v, &err);
      if (table[i].error)
        {
          g_assert_nonnull (err);
          g_assert_false (ok);
        }
      else
        {
          g_assert_cmpuint (table[i].val, ==, v);
          g_assert_true (ok);
        }
    }
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

static void
test_strv_make_n (TestRng *tt, gconstpointer user_data)
{
  g_auto(GStrv) empty = NULL;
  g_auto(GStrv) full = NULL;
  const char *check[] = {"voll", "voll", NULL};

  empty = bolt_strv_make_n (0, "nichts");
  g_assert_nonnull (empty);
  g_assert_null (*empty);

  full = bolt_strv_make_n (2, "voll");
  g_assert_nonnull (full);
  g_assert_nonnull (*full);
  g_assert_cmpuint (bolt_gstrv_length0 (full), ==, 2);
  bolt_assert_strv_equal (full, (GStrv) check, -1);
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
    {
      g_assert_cmpuint (bolt_strv_length (table[i].strv), ==, table[i].l);
      g_assert_cmpuint (bolt_gstrv_length0 (table[i].strv), ==, table[i].l);

      if (table[i].l == 0)
        g_assert_true (bolt_strv_isempty (table[i].strv));
      else
        g_assert_false (bolt_strv_isempty (table[i].strv));
    }
}

static void
test_strv_contains (TestRng *tt, gconstpointer user_data)
{

  const GStrv strv = MAKE_GSTRV ("a", "b", "c", "d", NULL);

  g_assert_false (bolt_strv_contains (NULL, "nonexistent"));
  g_assert_false (bolt_strv_contains (strv, "nonexistent"));

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
  guint N;
  guint k = 0;

  bolt_strv_permute (NULL);
  bolt_strv_permute (empty);

  g_assert_cmpuint (bolt_strv_length (empty), ==, 0U);

  tst = g_strdupv ((char **) ref);

  /* there are 4! = 24 possible permutations, do it
   * at least N = 4! and pick a rather large threshold
   * instead of a larger N */
  N = (4 * 3 * 2 * 1);

  for (guint i = 0; i < N; i++)
    {
      bolt_strv_permute (tst);
      if (bolt_strv_equal ((char **) ref, (char **) tst))
        k++;
    }

  g_debug ("permutation-test: %u of %u were equal", k, N);
  g_assert_cmpuint (k, <, 5);
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

#ifndef MAKE_GSTRV
#define MAKE_GSTRV(...) (GStrv) (const char *[]){ __VA_ARGS__}
#endif

static void
test_uuidv_check (TestRng *tt, gconstpointer user_data)
{
  g_autoptr(GError) err = NULL;
  char *empty[] = {NULL};
  const char *empty_entries[] = {"884c6edd-7118-4b21-b186-b02d396ecca0", "", NULL};
  const char *valid[] = {"884c6edd-7118-4b21-b186-b02d396ecca0", NULL};
  const GStrv invalid[] = {
    MAKE_GSTRV ("\n", NULL),
    MAKE_GSTRV ("884c6eddx7118x4b21xb186-b02d396ecca0", NULL),
    MAKE_GSTRV ("884c6edd-4b21-b186-b02d396ecca0", NULL),
    MAKE_GSTRV ("884c6edd-7118-4b21-b186-b02d396ecca0", "a", NULL),
  };
  gboolean ok;

  ok = bolt_uuidv_check (NULL, TRUE, &err);
  g_assert_true (ok);

  ok = bolt_uuidv_check (empty, TRUE, &err);
  g_assert_true (ok);

  ok = bolt_uuidv_check ((GStrv) empty_entries, TRUE, &err);
  g_assert_true (ok);

  ok = bolt_uuidv_check (NULL, FALSE, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  g_assert_false (ok);
  g_clear_error (&err);

  ok = bolt_uuidv_check (empty, FALSE, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  g_assert_false (ok);
  g_clear_error (&err);

  ok = bolt_uuidv_check ((GStrv) empty_entries, FALSE, &err);
  g_assert_error (err, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  g_assert_false (ok);
  g_clear_error (&err);

  ok = bolt_uuidv_check ((GStrv) valid, TRUE, &err);
  g_assert_true (ok);

  for (gsize i = 0; i < G_N_ELEMENTS (invalid); i++)
    {
      ok = bolt_uuidv_check (invalid[i], FALSE, &err);
      g_assert_error (err, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
      g_assert_false (ok);
      g_clear_error (&err);
    }
}

static void
test_term_fancy (TestRng *tt, gconstpointer user_data)
{
  skip_test_unless (bolt_is_fancy_terminal (),
                    "Terminal is not fancy");

  g_assert_cmpstr (bolt_color (ANSI_NORMAL), !=, "");
  g_assert_cmpstr (bolt_glyph (WARNING_SIGN), !=, "");
}

static void
test_term_plain (TestRng *tt, gconstpointer user_data)
{
  skip_test_if (bolt_is_fancy_terminal (),
                "Terminal is too fancy");

  g_assert_cmpstr (bolt_color (ANSI_NORMAL), ==, "");
  g_assert_cmpstr (bolt_glyph (WARNING_SIGN), !=, "");
}

static void
test_time (TestRng *tt, gconstpointer user_data)
{
  g_autofree char *str = NULL;

  str = bolt_epoch_format (0, "%Y");
  g_assert_cmpstr (str, ==, "1970");
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

  /* start in the middle */
  for (guint i = 0; i < 10; i++)
    {
      c = 0;
      bolt_nhlist_iter_init (&iter, &n[i]);
      while ((k = bolt_nhlist_iter_next (&iter)))
        {
          BoltList *p = bolt_nhlist_iter_node (&iter);
          g_assert_true (k == n + ((c + i) % 10));
          g_assert_true (k == p);
          c++;
        }

      g_assert_cmpuint (c, ==, 10);
      g_debug ("start[%u] %p: count: %u", i, &n[i], c);
    }
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

  g_resources_register (bolt_test_get_resource ());

  g_test_add ("/common/dbus/interface_info_find",
              TestDummy,
              NULL,
              NULL,
              test_dbus_interface_info_find,
              NULL);

  g_test_add ("/common/dbus/interface_info_lookup",
              TestDummy,
              NULL,
              NULL,
              test_dbus_interface_info_lookup,
              NULL);

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

  g_test_add ("/common/io/errors",
              TestIO,
              NULL,
              test_io_setup,
              test_io_errors,
              test_io_tear_down);

  g_test_add ("/common/io/verify",
              TestIO,
              NULL,
              test_io_setup,
              test_io_verify,
              test_io_tear_down);

  g_test_add ("/common/io/write_file_at",
              TestIO,
              NULL,
              test_io_setup,
              test_io_write_file_at,
              test_io_tear_down);

  g_test_add ("/common/io/file_write_all",
              TestIO,
              NULL,
              test_io_setup,
              test_io_file_write_all,
              test_io_tear_down);

  g_test_add ("/common/io/copy_bytes",
              TestIO,
              NULL,
              test_io_setup,
              test_io_copy_bytes,
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

  g_test_add ("/common/str/parse/int",
              TestRng,
              NULL,
              NULL,
              test_str_parse_int,
              NULL);

  g_test_add ("/common/str/parse/uint",
              TestRng,
              NULL,
              NULL,
              test_str_parse_uint,
              NULL);

  g_test_add ("/common/str/parse/uint64",
              TestRng,
              NULL,
              NULL,
              test_str_parse_uint64,
              NULL);

  g_test_add ("/common/str/parse/uint32",
              TestRng,
              NULL,
              NULL,
              test_str_parse_uint32,
              NULL);

  g_test_add ("/common/str/parse/boolean",
              TestRng,
              NULL,
              NULL,
              test_str_parse_boolean,
              NULL);

  g_test_add ("/common/str/set",
              TestRng,
              NULL,
              NULL,
              test_str_set,
              NULL);

  g_test_add ("/common/strv/make_n",
              TestRng,
              NULL,
              NULL,
              test_strv_make_n,
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

  g_test_add ("/common/uuidv/check",
              TestRng,
              NULL,
              NULL,
              test_uuidv_check,
              NULL);

  g_test_add ("/common/term/fancy",
              TestRng,
              NULL,
              NULL,
              test_term_fancy,
              NULL);

  g_test_add ("/common/term/plain",
              TestRng,
              NULL,
              NULL,
              test_term_plain,
              NULL);

  g_test_add ("/common/time",
              TestRng,
              NULL,
              NULL,
              test_time,
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

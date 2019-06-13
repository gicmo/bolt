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

#include "bolt-store.h"

#include "bolt-error.h"
#include "bolt-fs.h"
#include "bolt-io.h"
#include "bolt-key.h"
#include "bolt-rnd.h"
#include "bolt-str.h"

#include <gio/gio.h>

#include <string.h>

/* ************************************  */
/* BoltKey */

struct _BoltKey
{
  GObject object;

  /* the actual key plus the null char */
  char     data[BOLT_KEY_CHARS + 1];
  gboolean fresh;
};


enum {
  PROP_KEY_0,

  PROP_KEY_FRESH,

  PROP_KEY_LAST
};

static GParamSpec *key_props[PROP_KEY_LAST] = { NULL, };

G_DEFINE_TYPE (BoltKey,
               bolt_key,
               G_TYPE_OBJECT);


static void
bolt_key_finalize (GObject *object)
{
  BoltKey *key = BOLT_KEY (object);

  bolt_erase_n (key->data, sizeof (key->data));

  G_OBJECT_CLASS (bolt_key_parent_class)->finalize (object);
}

static void
bolt_key_init (BoltKey *key)
{
  memset (key->data, 0, sizeof (key->data));
}

static void
bolt_key_get_property (GObject    *object,
                       guint       prop_id,
                       GValue     *value,
                       GParamSpec *pspec)
{
  BoltKey *key = BOLT_KEY (object);

  switch (prop_id)
    {
    case PROP_KEY_FRESH:
      g_value_set_boolean (value, key->fresh);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bolt_key_set_property (GObject      *object,
                       guint         prop_id,
                       const GValue *value,
                       GParamSpec   *pspec)
{
  BoltKey *key = BOLT_KEY (object);

  switch (prop_id)
    {
    case PROP_KEY_FRESH:
      key->fresh = g_value_get_boolean (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bolt_key_class_init (BoltKeyClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = bolt_key_finalize;

  gobject_class->get_property = bolt_key_get_property;
  gobject_class->set_property = bolt_key_set_property;

  key_props[PROP_KEY_FRESH] =
    g_param_spec_boolean ("fresh",
                          NULL, NULL,
                          FALSE,
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_READWRITE |
                          G_PARAM_STATIC_NICK);

  g_object_class_install_properties (gobject_class,
                                     PROP_KEY_LAST,
                                     key_props);
}

/* internal methods */


/* public methods */
BoltKey  *
bolt_key_new (GError **error)
{
  BoltKey *key;
  BoltRng rng;
  char data[BOLT_KEY_BYTES];

  rng = bolt_get_random_data (data, BOLT_KEY_BYTES);

  /* fail if we can not be sure that we have good enough
   * random data, which is only guaranteed by getrandom */
  if (rng != BOLT_RNG_GETRANDOM)
    {
      g_set_error (error, BOLT_ERROR_FAILED, BOLT_ERROR_NOKEY,
                   "failed to create key: no random data");
      return NULL;
    }

  key = g_object_new (BOLT_TYPE_KEY, NULL);

  for (guint i = 0; i < BOLT_KEY_BYTES; i++)
    {
      char *pos = key->data + 2 * i;
      gulong n = sizeof (key->data) - 2 * i;
      g_snprintf (pos, n, "%02hhx", data[i]);
    }

  bolt_erase_n (data, sizeof (data));
  key->fresh = TRUE;

  return key;
}

gboolean
bolt_key_write_to (BoltKey      *key,
                   int           fd,
                   BoltSecurity *level,
                   GError      **error)
{
  g_autoptr(GError) err = NULL;
  gboolean ok;

  g_return_val_if_fail (BOLT_IS_KEY (key), FALSE);
  g_return_val_if_fail (fd > -1, FALSE);
  g_return_val_if_fail (level != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  *level = BOLT_SECURITY_USER;

  if (key->data[0] == '\0')
    return TRUE;

  ok = bolt_write_all (fd, key->data, BOLT_KEY_CHARS, &err);
  if (!ok && g_error_matches (err, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
    g_set_error_literal (error, BOLT_ERROR, BOLT_ERROR_BADKEY, "invalid key data");
  else if (!ok)
    bolt_error_propagate (error, &err);
  else if (!key->fresh) /* ok == True */
    *level = BOLT_SECURITY_SECURE;

  return ok;
}

gboolean
bolt_key_save_file (BoltKey *key,
                    GFile   *file,
                    GError **error)
{
  gboolean ok;

  ok = g_file_replace_contents (file,
                                key->data, BOLT_KEY_CHARS,
                                NULL, FALSE,
                                G_FILE_CREATE_PRIVATE,
                                NULL,
                                NULL, error);
  return ok;
}

BoltKey *
bolt_key_load_file (GFile   *file,
                    GError **error)
{
  g_autoptr(BoltKey) key = NULL;
  g_autofree char *path = NULL;
  gboolean ok;
  gsize len;
  int fd;

  g_return_val_if_fail (G_IS_FILE (file), NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  key = g_object_new (BOLT_TYPE_KEY, NULL);
  path = g_file_get_path (file);

  fd = bolt_open (path, O_CLOEXEC | O_RDONLY, 0, error);
  if (fd < 0)
    return NULL;

  memset (key->data, 0, sizeof (key->data));
  ok = bolt_read_all (fd, key->data, BOLT_KEY_CHARS, &len, error);
  close (fd);

  if (!ok)
    return NULL;

  /* empty key; NB: the kernel gives us "\n" for an empty key */
  if (len == 0 || (len == 1 && g_ascii_isspace (key->data[0])))
    {
      g_set_error_literal (error, BOLT_ERROR, BOLT_ERROR_NOKEY,
                           "key-file exists but contains no data");
      return NULL;
    }

  if (len != BOLT_KEY_CHARS)
    {
      g_set_error (error, BOLT_ERROR, BOLT_ERROR_BADKEY,
                   "unexpected key size (corrupt key?): %zu", len);
      return NULL;
    }

  key->fresh = FALSE;

  return g_steal_pointer (&key);
}

BoltKeyState
bolt_key_get_state (BoltKey *key)
{
  if (key == NULL)
    return BOLT_KEY_MISSING;

  g_return_val_if_fail (BOLT_IS_KEY (key), BOLT_KEY_UNKNOWN);

  return key->fresh ? BOLT_KEY_NEW : BOLT_KEY_HAVE;
}

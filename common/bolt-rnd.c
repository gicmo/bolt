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

#include "bolt-io.h"
#include "bolt-rnd.h"

#include <gio/gio.h>

#include <errno.h>
#include <string.h>
#if HAVE_FN_GETRANDOM
#include <sys/random.h>
# else
# define GRND_NONBLOCK 0
#endif

int
bolt_get_random_data (void *buf, gsize n)
{
  gboolean ok;

  ok = bolt_random_getrandom (buf, n, GRND_NONBLOCK, NULL);
  if (ok)
    return BOLT_RNG_GETRANDOM;

  ok = bolt_random_urandom (buf, n);

  if (ok)
    return BOLT_RNG_URANDOM;

  bolt_random_prng (buf, n);
  return BOLT_RNG_PRNG;
}

/* specific implementations */
gboolean
bolt_random_getrandom (void    *buf,
                       gsize    n,
                       unsigned flags,
                       GError **error)
{
  int r = -1;

  g_return_val_if_fail (buf != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

#if HAVE_FN_GETRANDOM
  r = getrandom (buf, n, flags);
#else
  errno = ENOSYS;
#endif

  if (r < 0)
    {
      g_set_error (error, G_IO_ERROR,
                   g_io_error_from_errno (errno),
                   "failed to get random data: %s",
                   g_strerror (errno));
      return FALSE;
    }

  return TRUE;
}

gboolean
bolt_random_urandom (void *buf, gsize n)
{
  gboolean ok;
  int rndfd;
  gsize len;

  g_return_val_if_fail (buf != NULL, FALSE);

  rndfd = bolt_open ("/dev/urandom", O_RDONLY | O_CLOEXEC | O_NOCTTY, 0, NULL);

  if (rndfd < 0)
    return FALSE;

  ok = bolt_read_all (rndfd, buf, n, &len, NULL);

  (void) close (rndfd);

  /* NB: accroding to the man page random(4), "when calling
     read(2) for the device /dev/urandom, reads of up to 256
     bytes will return as many bytes as are requested and will
     not be interrupted by a signal handler".
   */
  return ok && len == n;
}

void
bolt_random_prng (void *buf, gsize n)
{
  char *ptr = buf;
  const gsize l = n % sizeof (guint32);
  const gsize k = n - l;

  if (buf == NULL || n == 0)
    return;

  for (gsize i = 0; i < k; i += sizeof (guint32))
    {
      guint32 r = g_random_int ();
      memcpy (ptr + i, &r, sizeof (guint32));
    }

  if (l > 0)
    {
      guint32 r = g_random_int ();
      memcpy (ptr + k, &r, l);
    }
}

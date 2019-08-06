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

#include "bolt-log.h"
#include "bolt-str.h"
#include "bolt-time.h"
#include "bolt-unix.h"

#include <inttypes.h>

/* prototypes */
static void     watchdog_initable_iface_init (GInitableIface *iface);
static gboolean bolt_watchdog_initialize (GInitable    *initable,
                                          GCancellable *cancellable,
                                          GError      **error);

static gboolean bolt_watchdog_on_pulse (gpointer user_data);
/*  */
struct _BoltWatchdog
{
  GObject object;

  /*  */
  guint64 timeout;   /* the actual timeout (usec) */
  guint64 pulse;     /* the calculated pulse (sec) */
  guint   pulse_id;  /* source id for the pulse */

};

enum {
  PROP_0,

  PROP_TIMEOUT,
  PROP_PULSE,

  PROP_LAST
};

static GParamSpec *props[PROP_LAST] = { NULL, };


G_DEFINE_TYPE_WITH_CODE (BoltWatchdog,
                         bolt_watchdog,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                watchdog_initable_iface_init));

static void
bolt_watchdog_finalize (GObject *object)
{
  BoltWatchdog *dog = BOLT_WATCHDOG (object);

  g_clear_handle_id (&dog->pulse_id, g_source_remove);

  G_OBJECT_CLASS (bolt_watchdog_parent_class)->finalize (object);
}


static void
bolt_watchdog_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  BoltWatchdog *dog = BOLT_WATCHDOG (object);

  switch (prop_id)
    {
    case PROP_TIMEOUT:
      g_value_set_uint64 (value, dog->timeout);
      break;

    case PROP_PULSE:
      g_value_set_uint (value, dog->pulse);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bolt_watchdog_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  BoltWatchdog *dog = BOLT_WATCHDOG (object);

  switch (prop_id)
    {
    case PROP_TIMEOUT:
      dog->timeout = g_value_get_uint64 (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bolt_watchdog_init (BoltWatchdog *dog)
{
}

static void
bolt_watchdog_class_init (BoltWatchdogClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = bolt_watchdog_get_property;
  gobject_class->set_property = bolt_watchdog_set_property;
  gobject_class->finalize     = bolt_watchdog_finalize;

  props[PROP_TIMEOUT] =
    g_param_spec_uint64 ("timeout",
                         "Timeout", NULL,
                         0, G_MAXUINT64, 0,
                         G_PARAM_READABLE |
                         G_PARAM_STATIC_STRINGS);

  props[PROP_PULSE] =
    g_param_spec_uint ("pulse",
                       "Pulse", NULL,
                       0, G_MAXUINT - 1, 0,
                       G_PARAM_READABLE |
                       G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class,
                                     PROP_LAST,
                                     props);
}

static void
watchdog_initable_iface_init (GInitableIface *iface)
{
  iface->init = bolt_watchdog_initialize;
}

static gboolean
bolt_watchdog_initialize (GInitable    *initable,
                          GCancellable *cancellable,
                          GError      **error)
{
  BoltWatchdog *dog = BOLT_WATCHDOG (initable);
  guint64 quot, rem;
  guint pulse;
  guint tid;
  int r;

  r = bolt_sd_watchdog_enabled (&dog->timeout, error);

  if (r < 0)
    return FALSE;
  else if (r == 0)
    return TRUE;

  quot = dog->timeout / G_USEC_PER_SEC;
  rem = dog->timeout % G_USEC_PER_SEC;

  if (quot < 2 || quot >= G_MAXUINT)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "invalid timeout: %" G_GUINT64_FORMAT, dog->timeout);
      return FALSE;
    }

  if (rem != 0)
    bolt_warn (LOG_TOPIC ("watchdog"),
               "sub-second precision timeout: "
               "%" G_GUINT64_FORMAT ". Rounding down.", rem);

  /* we send a pulse/ping right in the middle of the timeout
   * period to ensure we never miss one */
  pulse = quot / 2;

  tid = g_timeout_add_seconds (pulse, bolt_watchdog_on_pulse, dog);
  dog->pulse_id = tid;
  dog->pulse = pulse;

  /* we have an active timeout enabled */
  bolt_info (LOG_TOPIC ("watchdog"), "enabled [pulse: %lus]", pulse);

  return TRUE;
}

/* internal methods */
static gboolean
bolt_watchdog_on_pulse (gpointer user_data)
{
  g_autoptr(GError) err = NULL;
  gboolean ok;
  gboolean sent;

  ok = bolt_sd_notify_literal ("WATCHDOG=1", &sent, &err);

  if (!ok)
    bolt_warn_err (err, LOG_TOPIC ("watchdog"), "failed to send ping");
  else
    bolt_debug (LOG_TOPIC ("watchdog"), "ping [sent: %s]",
                bolt_yesno (sent));

  return G_SOURCE_CONTINUE;
}

/* public methods */
BoltWatchdog  *
bolt_watchdog_new (GError **error)
{
  BoltWatchdog *watchdog;

  watchdog = g_initable_new (BOLT_TYPE_WATCHDOG,
                             NULL, error,
                             NULL);

  return watchdog;
}

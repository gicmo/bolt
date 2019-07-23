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

/*  */
struct _BoltWatchdog
{
  GObject object;

  /*  */
  guint watchdog_timeout;
  guint watchdog_id;

  /* */

};

enum {
  PROP_0,

  PROP_LAST
};

static GParamSpec *props[PROP_LAST] = { NULL, };


G_DEFINE_TYPE (BoltWatchdog,
               bolt_watchdog,
               G_TYPE_OBJECT);

static void
bolt_watchdog_finalize (GObject *object)
{
  //BoltWatchdog *watchdog = BOLT_WATCHDOG (object);

  G_OBJECT_CLASS (bolt_watchdog_parent_class)->finalize (object);
}


static void
bolt_watchdog_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  // BoltWatchdog *watchdog = BOLT_WATCHDOG (object);

  switch (prop_id)
    {
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
  // BoltWatchdog *watchdog = BOLT_WATCHDOG (object);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
bolt_watchdog_init (BoltWatchdog *watchdog)
{
}

static void
bolt_watchdog_class_init (BoltWatchdogClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = bolt_watchdog_get_property;
  gobject_class->set_property = bolt_watchdog_set_property;
  gobject_class->finalize     = bolt_watchdog_finalize;


}

/* public methods */
BoltWatchdog  *
bolt_watchdog_new (void)
{
  BoltWatchdog *watchdog;

  watchdog = g_object_new (BOLT_TYPE_WATCHDOG, NULL);

  return watchdog;
}

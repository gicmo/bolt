/*
 * Copyright © 2020 Red Hat, Inc
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

#include "bolt-wire.h"

#include "bolt-error.h"

#include <gio/gio.h>

G_DEFINE_BOXED_TYPE (BoltLinkSpeed, bolt_link_speed, bolt_link_speed_copy, g_free);

BoltLinkSpeed *
bolt_link_speed_copy (const BoltLinkSpeed *other)
{
  BoltLinkSpeed *copy = g_new (BoltLinkSpeed, 1);

  *copy = *other;
  return copy;
}

gboolean
bolt_link_speed_equal (const BoltLinkSpeed *a,
                       const BoltLinkSpeed *b)
{
  return
    a->rx.speed == b->rx.speed &&
    a->rx.lanes == b->rx.lanes &&
    a->tx.speed == b->tx.speed &&
    a->tx.lanes == b->tx.lanes;
}

GVariant *
bolt_link_speed_to_wire (BoltWireConv *conv,
                         const GValue *value,
                         GError      **error)
{
  GVariantBuilder builder;
  BoltLinkSpeed *link;


  link = g_value_get_boxed (value);

  g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{su}"));

  g_variant_builder_add (&builder, "{su}", "rx.speed", link->rx.speed);
  g_variant_builder_add (&builder, "{su}", "rx.lanes", link->rx.lanes);
  g_variant_builder_add (&builder, "{su}", "tx.speed", link->tx.speed);
  g_variant_builder_add (&builder, "{su}", "tx.lanes", link->tx.lanes);

  return g_variant_builder_end (&builder);
}

gboolean
bolt_link_speed_from_wire (BoltWireConv *conv,
                           GVariant     *wire,
                           GValue       *value,
                           GError      **error)
{
  BoltLinkSpeed link;
  gboolean ok;

  struct
  {
    const char *name;
    guint32    *target;
  } entries[] = {
    {"rx.speed", &link.rx.speed},
    {"rx.lanes", &link.rx.lanes},
    {"tx.speed", &link.tx.speed},
    {"tx.lanes", &link.tx.lanes},
  };

  for (unsigned i = 0; i < G_N_ELEMENTS (entries); i++)
    {
      const char *name = entries[i].name;
      guint32 *target = entries[i].target;

      ok = g_variant_lookup (wire, name, "u", target);
      if (!ok)
        {
          g_set_error (error, BOLT_ERROR, BOLT_ERROR_FAILED,
                       "Missing entry in LinkSpeed dict: %s",
                       name);
          return FALSE;
        }
    }

  g_value_set_boxed (value, &link);

  return ok;
}

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

#pragma once

#include <glib-object.h>

#include <bolt-glue.h>

G_BEGIN_DECLS

typedef struct BoltLinkSpeed_ BoltLinkSpeed;

struct BoltLinkSpeed_
{
  struct
  {
    struct
    {
      guint32 speed;
      guint32 lanes;
    } rx;

    struct
    {
      guint32 speed;
      guint32 lanes;
    } tx;
  };
};

GType bolt_link_speed_get_type (void);
#define BOLT_TYPE_LINK_SPEED (bolt_link_speed_get_type ())

BoltLinkSpeed *  bolt_link_speed_copy (const BoltLinkSpeed *ls);

gboolean         bolt_link_speed_equal (const BoltLinkSpeed *a,
                                        const BoltLinkSpeed *b);

GVariant *       bolt_link_speed_to_wire (BoltWireConv *conv,
                                          const GValue *value,
                                          GError      **error);

gboolean         bolt_link_speed_from_wire (BoltWireConv *conv,
                                            GVariant     *wire,
                                            GValue       *value,
                                            GError      **error);

G_END_DECLS

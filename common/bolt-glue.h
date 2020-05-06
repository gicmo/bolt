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

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS


GParamSpec *          bolt_param_spec_override (GObjectClass *object_class,
                                                const char   *name);

gboolean              bolt_str_parse_by_pspec (GParamSpec *spec,
                                               const char *str,
                                               GValue     *val,
                                               GError    **error);

GPtrArray *           bolt_properties_for_type (GType target);

gboolean              bolt_properties_find (GPtrArray   *specs,
                                            const char  *name,
                                            GParamSpec **spec,
                                            GError     **error);

/* wire protocol variant/value conversions  */
typedef struct _BoltWireConv BoltWireConv;

typedef GVariant *  (*BoltConvToWire) (BoltWireConv *conv,
                                       const GValue *value,
                                       GError      **error);

typedef  gboolean (*BoltConvFromWire) (BoltWireConv *conv,
                                       GVariant     *wire,
                                       GValue       *value,
                                       GError      **error);

BoltWireConv *        bolt_wire_conv_ref (BoltWireConv *conv);

void                  bolt_wire_conv_unref (BoltWireConv *conv);

const GVariantType *  bolt_wire_conv_get_wire_type (BoltWireConv *conv);

const GParamSpec *    bolt_wire_conv_get_prop_spec (BoltWireConv *conv);

gboolean              bolt_wire_conv_is_native (BoltWireConv *conv);

const char *          bolt_wire_conv_describe (BoltWireConv *conv);

BoltWireConv *        bolt_wire_conv_for (const GVariantType *wire_type,
                                          GParamSpec         *prop_spec);

BoltWireConv *        bolt_wire_conv_custom (const GVariantType *wire_type,
                                             GParamSpec         *prop_spec,
                                             const char         *custom_id,
                                             BoltConvToWire      to_wire,
                                             BoltConvFromWire    from_wire);

GVariant *            bolt_wire_conv_to_wire (BoltWireConv *conv,
                                              const GValue *value,
                                              GError      **error);

gboolean              bolt_wire_conv_from_wire (BoltWireConv *conv,
                                                GVariant     *wire,
                                                GValue       *value,
                                                GError      **error);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (BoltWireConv, bolt_wire_conv_unref)

#if !GLIB_CHECK_VERSION (2, 57, 1)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GParamSpec, g_param_spec_unref)
#endif

G_END_DECLS

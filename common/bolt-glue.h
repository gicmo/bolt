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

#include <gio/gio.h>

G_BEGIN_DECLS


GParamSpec *          bolt_param_spec_override (GObjectClass *object_class,
                                                const char   *name);

GPtrArray *           bolt_properties_for_type (GType target);

gboolean              bolt_properties_find (GPtrArray   *specs,
                                            const char  *name,
                                            GParamSpec **spec,
                                            GError     **error);


G_END_DECLS

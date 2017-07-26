/*** BEGIN file-header ***/
/* -*- mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Christian J. Kellner <christian@kellner.me>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <gio/gio.h>

#include "device.h"
#include "manager.h"

#include "enums.h"

/*** END file-header ***/

/*** BEGIN file-production ***/
/* enumerations from "@filename@" */
/*** END file-production ***/

/*** BEGIN value-header ***/
GType
@enum_name@_get_type (void)
{
	static volatile gsize g_define_type_id__volatile = 0;

	if (g_once_init_enter (&g_define_type_id__volatile)) {
		static const G@Type@Value values[] = {
/*** END value-header ***/

/*** BEGIN value-production ***/
			{@VALUENAME@, "@VALUENAME@", "@valuenick@"},
/*** END value-production ***/

/*** BEGIN value-tail ***/
			{0, NULL, NULL}
		};
		GType g_define_type_id =
		    g_@type@_register_static (g_intern_static_string ("@EnumName@"),
					      values);
		g_once_init_leave (&g_define_type_id__volatile,
				   g_define_type_id);
	}

	return g_define_type_id__volatile;
}

/*** END value-tail ***/

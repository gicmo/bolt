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

#include "bolt-glue.h"

#include "bolt-str.h"


GPtrArray *
bolt_properties_for_type (GType target)
{
  GObjectClass *klass;
  GParamSpec **specs;
  GPtrArray *props;
  guint n;

  klass = g_type_class_ref (target);
  specs = g_object_class_list_properties (klass, &n);
  props = g_ptr_array_new_full (n, (GDestroyNotify) g_param_spec_unref);

  for (guint i = 0; i < n; i++)
    {
      GParamSpec *s = specs[i];

      if (s->owner_type != target)
        continue;

      g_ptr_array_add (props, g_param_spec_ref (s));
    }

  return props;
}

gboolean
bolt_properties_find (GPtrArray   *specs,
                      const char  *name,
                      GParamSpec **spec,
                      GError     **error)
{

  if (name == NULL || specs == NULL)
    return FALSE;

  for (guint i = 0; i < specs->len; i++)
    {
      GParamSpec *s = g_ptr_array_index (specs, i);

      if (g_str_equal (name, s->name) ||
          bolt_streq (name, g_param_spec_get_nick (s)))
        {
          *spec = s;
          return TRUE;
        }
    }

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
               "property '%s' not found", name);

  return FALSE;
}

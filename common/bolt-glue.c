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


GParamSpec *
bolt_param_spec_override (GObjectClass *klass,
                          const char   *name)
{
  GObjectClass *parent_class = NULL;
  GParamSpec *base = NULL;
  GType parent;
  GType type;
  guint n;

  g_return_val_if_fail (G_IS_OBJECT_CLASS (klass), NULL);
  g_return_val_if_fail (name != NULL, NULL);

  type = G_OBJECT_CLASS_TYPE (klass);
  parent = g_type_parent (type);

  parent_class = g_type_class_peek (parent);
  if (parent_class)
    base = g_object_class_find_property (parent_class, name);

  if (base == NULL)
    {
      g_autofree GType *ifaces = g_type_interfaces (type, &n);

      while (n-- && base == NULL)
        {
          gpointer iface = g_type_default_interface_peek (ifaces[n]);

          if (iface)
            base = g_object_interface_find_property (iface, name);
        }
    }

  if (base == NULL)
    {
      g_critical ("Could not override unknown property: '%s::%s'",
                  G_OBJECT_CLASS_NAME (klass), name);
      return NULL;
    }

  return g_param_spec_override (name, base);
}

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

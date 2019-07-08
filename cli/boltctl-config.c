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

#include "boltctl.h"
#include "boltctl-cmds.h"

#include "bolt-error.h"
#include "bolt-glue.h"
#include "bolt-str.h"

#include <errno.h>
#include <stdlib.h>

static gboolean
type_for_name (const char *name,
               GType      *type,
               GError    **error)
{
  if (bolt_strcaseeq (name, "global") ||
      bolt_strcaseeq (name, "daemon"))
    {
      *type = BOLT_TYPE_CLIENT;
      return TRUE;
    }
  if (bolt_strcaseeq (name, "domain"))
    {
      *type = BOLT_TYPE_DOMAIN;
      return TRUE;
    }
  else if (bolt_strcaseeq (name, "device"))
    {
      *type = BOLT_TYPE_DEVICE;
      return TRUE;
    }

  g_set_error (error, BOLT_ERROR, BOLT_ERROR_FAILED,
               "unknown class '%s'", name);

  return FALSE;
}

static const char *
name_for_type (GType type)
{
  if (type == BOLT_TYPE_CLIENT)
    return "global";
  else if (type == BOLT_TYPE_DOMAIN)
    return "domain";
  else if (type == BOLT_TYPE_DEVICE)
    return "device";
  else
    return "unknown";
}

static BoltProxy *
target_for_type (BoltClient *client,
                 GType       type,
                 const char *name,
                 GError    **error)
{
  BoltProxy *target = NULL;

  if (type == BOLT_TYPE_CLIENT)
    target = g_object_ref (BOLT_PROXY (client));
  else if (type == BOLT_TYPE_DOMAIN)
    target = (BoltProxy *) bolt_client_find_domain (client, name, error);
  else if (type == BOLT_TYPE_DEVICE)
    target = (BoltProxy *) bolt_client_find_device (client, name, error);
  else
    g_set_error (error, BOLT_ERROR, BOLT_ERROR_FAILED,
                 "'%s' not handled", name);

  return target;
}

static int
property_get (BoltProxy *proxy, GParamSpec *spec, GError **error)
{
  g_auto(GValue) prop_val = G_VALUE_INIT;
  g_auto(GValue) str_val = G_VALUE_INIT;
  g_autofree char *val = NULL;
  gboolean ok;

  g_value_init (&prop_val, G_PARAM_SPEC_VALUE_TYPE (spec));
  g_value_init (&str_val, G_TYPE_STRING);

  ok = bolt_proxy_get_dbus_property (proxy, spec, &prop_val);
  if (!ok)
    {
      g_set_error (error, BOLT_ERROR, BOLT_ERROR_FAILED,
                   "Could not get property");
      return EXIT_FAILURE;
    }

  if (G_IS_PARAM_SPEC_ENUM (spec))
    val = g_strdup (bolt_enum_to_string (spec->value_type,
                                         g_value_get_enum (&prop_val),
                                         error));
  else if (G_IS_PARAM_SPEC_FLAGS (spec))
    val = bolt_flags_to_string (spec->value_type,
                                g_value_get_flags (&prop_val),
                                error);
  else if (G_IS_PARAM_SPEC_BOOLEAN (spec))
    val = g_strdup (bolt_yesno (g_value_get_boolean (&prop_val)));
  else if (g_value_transform (&prop_val, &str_val))
    val = g_value_dup_string (&str_val);
  else
    val = g_strdup_value_contents (&prop_val);

  if (val == NULL)
    return EXIT_FAILURE;

  g_print ("%s\n", val);
  return EXIT_SUCCESS;
}

static int
property_set (BoltProxy  *proxy,
              GParamSpec *spec,
              const char *str,
              GError    **error)
{
  g_auto(GValue) val = G_VALUE_INIT;
  gboolean ok;

  ok = bolt_str_parse_by_pspec (spec, str, &val, error);
  if (!ok)
    return EXIT_FAILURE;

  ok = bolt_proxy_set (proxy, spec, &val, NULL, error);

  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* describe */

static void
describe_properties (GType type)
{
  g_autoptr(GPtrArray) props = NULL;
  const char *prefix = name_for_type (type);

  props = bolt_properties_for_type (type);

  for (guint i = 0; i < props->len; i++)
    {
      GParamSpec *spec = g_ptr_array_index (props, i);
      const char *desc = g_param_spec_get_blurb (spec);
      gboolean can_write = spec->flags & G_PARAM_WRITABLE;
      const char *rw = can_write ? "rw" : "r-";

      g_print ("%s %s.%-14s %s\n", rw, prefix, spec->name, desc);
    }
}

static int
do_describe (BoltClient *client, int argc, char **argv)
{
  g_autoptr(GError) err = NULL;
  gboolean ok;

  ok = check_argc (argc, 0, 1, &err);
  if (!ok)
    return usage_error (err);

  if (argc == 1) /* boltctl config --describe */
    {
      describe_properties (BOLT_TYPE_CLIENT);
      describe_properties (BOLT_TYPE_DOMAIN);
      describe_properties (BOLT_TYPE_DEVICE);
    }
  else if (argc == 2) /* boltctl config --describe {global,device,domain} */
    {
      GType type;

      ok = type_for_name (argv[1], &type, &err);
      if (!ok)
        return usage_error (err);

      describe_properties (type);
    }

  return 0;
}

static gboolean
parse_option (const char *opt_str,
              GType      *type_out,
              char      **prop_out,
              GError    **error)
{
  g_auto(GStrv) comps = NULL;
  GType type = 0;
  gboolean ok;
  guint n;

  comps = g_strsplit (opt_str, ".", 2);
  n = g_strv_length (comps);

  *prop_out = NULL;

  /* Determine the fine structure of OPTION, i.e. is it
   * type.property or property */
  switch (n)
    {
    case 1:
      type = BOLT_TYPE_CLIENT;
      *prop_out = g_steal_pointer (&comps[0]);
      ok = TRUE;
      break;

    case 2:
      ok = type_for_name (comps[0], &type, error);
      *prop_out = g_steal_pointer (&comps[1]);
      break;

    default:
      g_set_error (error, BOLT_ERROR, BOLT_ERROR_FAILED,
                   "invalid OPTION string '%s'", opt_str);
      ok = FALSE;
      break;
    }

  *type_out = type;

  return ok;
}

/* */
typedef enum {
  ACTION_GET = 1,
  ACTION_SET = 2,
} Action;

static const char *summary =
  "Describing available items: \n"
  "  config --describe [global|domain|device]\n"
  "\n"
  "Getting items:\n"
  "  config KEY\n"
  "  config <domain|device>.KEY TARGET\n"
  "\n"
  "Setting items:\n"
  "  config KEY VALUE\n"
  "  config <domain|device>.KEY TARGET VALUE\n";

int
config (BoltClient *client, int argc, char **argv)
{
  g_autoptr(GOptionContext) optctx = NULL;
  g_autoptr(GPtrArray) props = NULL;
  g_autoptr(BoltProxy) target = NULL;
  g_autoptr(GError) err = NULL;
  g_autofree char *pstr = NULL;
  const char *value = NULL;
  gboolean describe = FALSE;
  gboolean ok = FALSE;
  GParamSpec *prop;
  Action action;
  GType type = 0;
  int res = EXIT_FAILURE;
  GOptionEntry options[] = {
    { "describe", 'd', 0, G_OPTION_ARG_NONE, &describe, "Describe options", NULL },
    { NULL }
  };

  optctx = g_option_context_new ("- Inspect and modify options");
  g_option_context_add_main_entries (optctx, options, NULL);
  g_option_context_set_summary (optctx, summary);
  g_option_context_set_strict_posix (optctx, TRUE);

  if (!g_option_context_parse (optctx, &argc, &argv, &err))
    return usage_error (err);

  if (describe)
    return do_describe (client, argc, argv);

  /* get or set */
  ok = check_argc (argc, 1, 3, &err);
  if (!ok)
    return usage_error (err);

  ok = parse_option (argv[1], &type, &pstr, &err);
  if (!ok)
    return usage_error (err);

  props = bolt_properties_for_type (type);
  ok = bolt_properties_find (props, pstr, &prop, &err);

  if (!ok)
    return usage_error (err);

  if (argc == 2)
    {
      /* boltctl config <property> */
      target = g_object_ref (BOLT_PROXY (client));
      action = ACTION_GET;
    }
  else if (argc == 3 && type != BOLT_TYPE_CLIENT)
    {
      /* boltctl config <{device, domain}.property> <target> */
      target = target_for_type (client, type, argv[2], &err);
      action = ACTION_GET;
    }
  else if (argc == 3 && type == BOLT_TYPE_CLIENT)
    {
      /* boltctl config <property> <value> */
      target = g_object_ref (BOLT_PROXY (client));
      action = ACTION_SET;
      value = argv[2];
    }
  else if (argc == 4)
    {
      /* boltctl config <{device, domain}.property> <target> <value> */
      target = target_for_type (client, type, argv[2], &err);
      action = ACTION_SET;
      value = argv[3];
    }

  if (target == NULL)
    return usage_error (err); /* must be set if target == NULL */

  /* action must either be GET or SET */
  if (action == ACTION_GET)
    res = property_get (target, prop, &err);
  else if (action == ACTION_SET)
    res = property_set (target, prop, value, &err);

  if (res == EXIT_FAILURE)
    report_error (NULL, err);

  return res;
}

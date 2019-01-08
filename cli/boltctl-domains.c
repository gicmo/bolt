/*
 * Copyright © 2017 Red Hat, Inc
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

#include "boltctl-cmds.h"
#include "boltctl-uidfmt.h"

#include "bolt-str.h"

#include <stdlib.h>

/* domain related commands */
static void
print_domain (BoltDomain *domain, gboolean verbose)
{
  g_auto(GStrv) bootacl = NULL;
  const char *tree_branch;
  const char *tree_right;
  const char *tree_cont;
  const char *tree_space;
  const char *uid;
  const char *name;
  const char *syspath;
  const char *security;
  BoltSecurity sl;
  gboolean online;
  gboolean iommu;

  tree_branch = bolt_glyph (TREE_BRANCH);
  tree_right = bolt_glyph (TREE_RIGHT);
  tree_cont = bolt_glyph (TREE_VERTICAL);
  tree_space = bolt_glyph (TREE_SPACE);

  uid = bolt_domain_get_uid (domain);
  name = bolt_domain_get_id (domain);
  sl = bolt_domain_get_security (domain);

  syspath = bolt_domain_get_syspath (domain);
  security = bolt_security_to_string (sl);
  bootacl = bolt_domain_get_bootacl (domain);
  online = syspath != NULL;

  if (online)
    {
      g_print (" %s%s%s ",
               bolt_color (ANSI_GREEN),
               bolt_glyph (BLACK_CIRCLE),
               bolt_color (ANSI_NORMAL));
    }
  else
    {
      g_print (" %s ", bolt_glyph (WHITE_CIRCLE));
    }

  g_print ("%s %s", (name ? : "domain"), format_uid (uid));
  g_print ("\n");

  if (verbose)
    g_print ("   %s online:   %s\n", tree_branch, bolt_yesno (online));

  if (verbose && syspath != NULL)
    g_print ("   %s syspath:  %s\n", tree_branch, syspath);

  if (bootacl)
    {
      guint acl_max = g_strv_length ((char **) bootacl);
      guint used = 0;
      guint i;

      for (i = 0; i < acl_max; i++)
        if (!bolt_strzero (bootacl[i]))
          used++;

      g_print ("   %s bootacl:  %u/%u\n", tree_branch, used, acl_max);

      for (i = 0; i < acl_max; i++)
        {
          const char *tree_sym = used > 1 ? tree_branch : tree_right;

          if (bolt_strzero (bootacl[i]))
            continue;

          g_print ("   %s  %s[%u]", tree_cont, tree_sym, i);
          g_print (" %s\n", format_uid (bootacl[i]));
          used--;
        }
    }

  iommu = bolt_domain_has_iommu (domain);

  g_print ("   %s security: ", tree_right);

  if (bolt_security_is_interactive (sl) && iommu)
    g_print ("iommu+%s", security);
  else if (bolt_security_allows_pcie (sl) && iommu)
    g_print ("iommu");
  else
    g_print ("%s", security);

  g_print ("\n");

  if (verbose)
    {
      g_print ("   %s %s iommu: %s\n", tree_space, tree_branch,
               bolt_yesno (iommu));

      g_print ("   %s %s level: %s\n", tree_space, tree_right,
               security);

    }

  g_print ("\n");
}

int
list_domains (BoltClient *client, int argc, char **argv)
{
  g_autoptr(GOptionContext) optctx = NULL;
  g_autoptr(GError) err = NULL;
  g_autoptr(GPtrArray) domains = NULL;
  gboolean details = FALSE;
  GOptionEntry options[] = {
    { "verbose", 'v', 0, G_OPTION_ARG_NONE, &details, "Show more details", NULL },
    { NULL }
  };

  optctx = g_option_context_new ("- List thunderbolt domains");
  g_option_context_add_main_entries (optctx, options, NULL);

  if (!g_option_context_parse (optctx, &argc, &argv, &err))
    return usage_error (err);

  domains = bolt_client_list_domains (client, NULL, &err);

  if (domains == NULL)
    {
      g_warning ("Could not list domains: %s", err->message);
      domains = g_ptr_array_new_with_free_func (g_object_unref);
    }

  for (guint i = 0; i < domains->len; i++)
    {
      BoltDomain *dom = g_ptr_array_index (domains, i);
      print_domain (dom, details);
    }

  return EXIT_SUCCESS;
}

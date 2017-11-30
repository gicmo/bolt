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

#include "bolt-dbus.h"
#include "bolt-manager.h"
#include "bolt-term.h"

#include <gio/gio.h>

#include <locale.h>
#include <stdlib.h>


/* globals */
static BoltManager *manager = NULL;
static GMainLoop *main_loop = NULL;
static guint name_owner_id = 0;

#define TIME_MAXFMT 255
static void
log_handler (const gchar   *log_domain,
             GLogLevelFlags log_level,
             const gchar   *message,
             gpointer       user_data)
{
  const char *normal = bolt_color (ANSI_NORMAL);
  const char *fg = normal;
  gchar the_time[TIME_MAXFMT];
  time_t now;
  struct tm *tm;

  time (&now);
  tm = localtime (&now);

  if (tm && strftime (the_time, sizeof (the_time), "%T", tm) > 0)
    {
      const char *gray = bolt_color (ANSI_HIGHLIGHT_BLACK);
      g_printerr ("%s%s%s ", gray, the_time, normal);
    }

  if (log_level == G_LOG_LEVEL_CRITICAL ||
      log_level == G_LOG_LEVEL_ERROR)
    fg = bolt_color (ANSI_RED);
  else if (log_level == G_LOG_LEVEL_WARNING)
    fg = bolt_color (ANSI_YELLOW);
  else if (log_level == G_LOG_LEVEL_INFO)
    fg = bolt_color (ANSI_BLUE);

  g_printerr ("%s%s%s\n", fg, message, normal);
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
  g_autoptr(GError) error = NULL;

  g_debug ("Got the bus [%s]", name);
  /*  */
  manager = g_initable_new (BOLT_TYPE_MANAGER,
                            NULL, &error,
                            NULL);

  if (manager == NULL)
    {
      g_printerr ("Could not create manager: %s", error->message);
      exit (EXIT_FAILURE);
    }

  if (!bolt_manager_export (manager, connection, &error))
    g_warning ("error: %s", error->message);

}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
  g_debug ("Got the name");
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
  g_debug ("Lost the name. Shutting down...");
  g_clear_object (&manager);
  g_bus_unown_name (name_owner_id);
  g_main_loop_quit (main_loop);
}

int
main (int argc, char **argv)
{
  GOptionContext *context;
  gboolean replace = FALSE;
  gboolean verbose = FALSE;
  gboolean show_version = FALSE;
  gboolean session_bus = FALSE;
  GBusType bus_type = G_BUS_TYPE_SYSTEM;
  GBusNameOwnerFlags flags;

  g_autoptr(GError) error = NULL;
  const GOptionEntry options[] = {
    { "replace", 'r', 0, G_OPTION_ARG_NONE, &replace,  "Replace old daemon.", NULL },
    { "session-bus", 0, 0, G_OPTION_ARG_NONE, &session_bus, "Use the session bus.", NULL},
    { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,  "Enable debug output.", NULL },
    { "version", 0, 0, G_OPTION_ARG_NONE, &show_version, "Print daemon version.", NULL},
    { NULL }
  };

  setlocale (LC_ALL, "");
  g_setenv ("GIO_USE_VFS", "local", TRUE);
  g_set_prgname (argv[0]);

  /* print all but debug messages */
  g_log_set_handler (G_LOG_DOMAIN,
                     G_LOG_LEVEL_MASK & ~G_LOG_LEVEL_DEBUG,
                     log_handler,
                     NULL);

  context = g_option_context_new ("");

  g_option_context_set_summary (context, "Thunderbolt system daemon");
  g_option_context_add_main_entries (context, options, PACKAGE_NAME);

  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_printerr ("%s: %s", g_get_application_name (), error->message);
      g_printerr ("\n");
      g_printerr ("Try \"%s --help\" for more information.",
                  g_get_prgname ());
      g_printerr ("\n");
      g_option_context_free (context);
      return EXIT_FAILURE;
    }

  if (show_version)
    {
      g_print (PACKAGE_NAME " " PACKAGE_VERSION "\n");
      return EXIT_SUCCESS;
    }

  if (verbose)
    g_log_set_handler (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, log_handler, NULL);

  g_debug (PACKAGE_NAME " " PACKAGE_VERSION " starting up.");

  flags = G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT;
  if (replace)
    flags |= G_BUS_NAME_OWNER_FLAGS_REPLACE;

  if (session_bus)
    bus_type = G_BUS_TYPE_SESSION;

  name_owner_id = g_bus_own_name (bus_type,
                                  BOLT_DBUS_NAME,
                                  flags,
                                  on_bus_acquired,
                                  on_name_acquired,
                                  on_name_lost,
                                  NULL,
                                  NULL);

  main_loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (main_loop);

  /* When all is said and done, more is said then done.  */
  g_main_loop_unref (main_loop);

  return EXIT_SUCCESS;
}

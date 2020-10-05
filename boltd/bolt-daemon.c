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
#include "bolt-log.h"
#include "bolt-manager.h"
#include "bolt-names.h"
#include "bolt-str.h"
#include "bolt-term.h"

#include <glib-unix.h>
#include <gio/gio.h>

#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

/* globals */
static BoltManager *manager = NULL;
static GMainLoop *main_loop = NULL;
static guint name_owner_id = 0;
static guint sigterm_id = 0;


static gboolean
handle_sigterm (gpointer user_data)
{
  bolt_debug (LOG_TOPIC ("signal"), "got SIGTERM; shutting down...");

  if (g_main_loop_is_running (main_loop))
    g_main_loop_quit (main_loop);

  sigterm_id = 0;
  return G_SOURCE_REMOVE;
}

static void
install_signal_hanlder (void)
{
  g_autoptr(GSource) source = NULL;

  source = g_unix_signal_source_new (SIGTERM);

  if (source == NULL)
    {
      bolt_warn (LOG_TOPIC ("signal"), "failed installing SIGTERM handler: %s",
                 g_strerror (errno));
      return;
    }

  g_source_set_callback (source, handle_sigterm, NULL, NULL);
  sigterm_id = g_source_attach (source, NULL);
  bolt_debug (LOG_TOPIC ("signal"), "SIGTERM handler installed [%u]",
              sigterm_id);
}

typedef struct _LogCfg
{
  gboolean debug;
  gboolean journal;
  char     session_id[33];
} LogCfg;

static GLogWriterOutput
daemon_logger (GLogLevelFlags   level,
               const GLogField *fields,
               gsize            n_fields,
               gpointer         user_data)
{
  g_autoptr(BoltLogCtx) ctx = NULL;
  GLogWriterOutput res = G_LOG_WRITER_UNHANDLED;
  LogCfg *log = user_data;

  g_return_val_if_fail (fields != NULL, G_LOG_WRITER_UNHANDLED);
  g_return_val_if_fail (n_fields > 0, G_LOG_WRITER_UNHANDLED);

  ctx = bolt_log_ctx_acquire (fields, n_fields);

  if (ctx == NULL)
    return G_LOG_WRITER_UNHANDLED;

  /* replace the log context field with the session id */
  bolt_log_ctx_set_id (ctx, log->session_id);

  if (level & G_LOG_LEVEL_DEBUG && log->debug == FALSE)
    {
      const char *domain = blot_log_ctx_get_domain (ctx);
      const char *env = g_getenv ("G_MESSAGES_DEBUG");

      if (!env || !domain || !strstr (env, domain))
        return G_LOG_WRITER_UNHANDLED;
    }

  if (fileno (stderr) < 0)
    return G_LOG_WRITER_UNHANDLED;

  if (log->journal || g_log_writer_is_journald (fileno (stderr)))
    res = bolt_log_journal (ctx, level, 0);

  if (res == G_LOG_WRITER_UNHANDLED)
    res = bolt_log_stdstream (ctx, level, 0);

  return res;
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
  g_autoptr(GError) error = NULL;

  bolt_debug (LOG_TOPIC ("dbus"), "got the bus [%s]", name);
  /*  */
  manager = g_initable_new (BOLT_TYPE_MANAGER,
                            NULL, &error,
                            NULL);

  if (manager == NULL)
    {
      bolt_error (LOG_ERR (error), "could not create manager");
      exit (EXIT_FAILURE);
    }

  if (!bolt_manager_export (manager, connection, &error))
    bolt_warn_err (error, LOG_TOPIC ("dbus"), "error exporting the manager");

}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
  bolt_debug (LOG_TOPIC ("dbus"), "got the name");
  bolt_manager_got_the_name (manager);
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
  bolt_debug (LOG_TOPIC ("dbus"), "name lost; shutting down...");

  if (g_main_loop_is_running (main_loop))
    g_main_loop_quit (main_loop);
}

int
main (int argc, char **argv)
{
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GError) error = NULL;
  gboolean replace = FALSE;
  gboolean show_version = FALSE;
  gboolean session_bus = FALSE;
  GBusType bus_type = G_BUS_TYPE_SYSTEM;
  GBusNameOwnerFlags flags;
  LogCfg log = { FALSE, FALSE, };
  const GOptionEntry options[] = {
    { "replace", 'r', 0, G_OPTION_ARG_NONE, &replace,  "Replace old daemon.", NULL },
    { "session-bus", 0, 0, G_OPTION_ARG_NONE, &session_bus, "Use the session bus.", NULL},
    { "verbose", 'v', 0, G_OPTION_ARG_NONE, &log.debug,  "Enable debug output.", NULL },
    { "journal", 0, 0, G_OPTION_ARG_NONE, &log.journal, "Force logging to the journal.", NULL},
    { "version", 0, 0, G_OPTION_ARG_NONE, &show_version, "Print daemon version.", NULL},
    { NULL }
  };

  install_signal_hanlder ();

  setlocale (LC_ALL, "");
  g_setenv ("GIO_USE_VFS", "local", TRUE);
  g_set_prgname (argv[0]);

  g_log_set_writer_func (daemon_logger, &log, NULL);

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

  /* setup logging  */
  if (log.debug == FALSE && g_getenv ("G_MESSAGES_DEBUG"))
    {
      const char *domains = g_getenv ("G_MESSAGES_DEBUG");
      log.debug = bolt_streq (domains, "all");
    }

  bolt_log_gen_id (log.session_id);

  bolt_dbus_ensure_resources ();

  bolt_msg (LOG_DIRECT (BOLT_LOG_VERSION, PACKAGE_VERSION),
            LOG_ID (STARTUP),
            PACKAGE_NAME " " PACKAGE_VERSION " starting up.");

  bolt_debug ("session id is %s", log.session_id);

  /* hop on the bus, Gus */
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

  /* we are shutting down */
  if (name_owner_id > 0)
    {
      g_bus_unown_name (name_owner_id);
      name_owner_id = 0;
    }

  g_clear_object (&manager);

  bolt_debug ("shutdown complete");

  return EXIT_SUCCESS;
}

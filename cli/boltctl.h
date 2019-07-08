/*
 * Copyright © 2018 Red Hat, Inc
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

#include "bolt-client.h"
#include "bolt-term.h"

G_BEGIN_DECLS

typedef int (*run_t)(BoltClient *client,
                     int         argc,
                     char      **argv);

typedef struct SubCommand
{
  const char *name;
  run_t       fn;
  const char *desc;
} SubCommand;

char *              subcommands_make_summary (const SubCommand *cmds);
const SubCommand *  subcommands_find (const SubCommand *cmds,
                                      const char       *cmdname,
                                      GError          **error);
int                 subcommand_run (const SubCommand *cmd,
                                    BoltClient       *client,
                                    int               argc,
                                    char            **argv);

gboolean check_argc (int      argc,
                     int      lower,
                     int      upper,
                     GError **error);

int      usage_error (GError *error);
int      usage_error_need_arg (const char *arg);
int      usage_error_too_many_args (void);
int      report_error (const char *prefix,
                       GError     *error);

void     print_device (BoltDevice *dev,
                       gboolean    verbose);

G_END_DECLS

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

#include "boltctl.h"

G_BEGIN_DECLS

typedef int (*BoltctlCommand) (BoltClient *client,
                               int         argc,
                               char      **argv);

int authorize (BoltClient *client,
               int         argc,
               char      **argv);
int config (BoltClient *client,
            int         argc,
            char      **argv);
int enroll (BoltClient *client,
            int         argc,
            char      **argv);
int forget (BoltClient *client,
            int         argc,
            char      **argv);
int info (BoltClient *client,
          int         argc,
          char      **argv);
int list_devices (BoltClient *client,
                  int         argc,
                  char      **argv);
int list_domains (BoltClient *client,
                  int         argc,
                  char      **argv);
int monitor (BoltClient *client,
             int         argc,
             char      **argv);
int power (BoltClient *client,
           int         argc,
           char      **argv);

G_END_DECLS

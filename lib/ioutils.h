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


#ifndef __TB_IOUTILS_H__
#define __TB_IOTUILS_H__

#include <glib.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

G_BEGIN_DECLS G_DEFINE_AUTOPTR_CLEANUP_FUNC (DIR, closedir);

int tb_open (const char *path,
             int         flags,
             GError    **error);

gboolean tb_close (int      fd,
                   GError **error);

gboolean tb_write_char (int      fd,
                        char     data,
                        GError **error);

ssize_t tb_read_all (int      fd,
                     void    *buffer,
                     gsize    nbyte,
                     GError **error);

DIR *tb_opendir (const char *path,
                 GError    **error);

int tb_openat (DIR       * d,
               const char *path,
               int         oflag,
               GError    **error);

gboolean tb_verify_uid (int         fd,
                        const char *uid,
                        GError    **error);

G_END_DECLS
#endif

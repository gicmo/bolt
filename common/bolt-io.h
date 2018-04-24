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

#pragma once

#include <glib.h>

#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

G_BEGIN_DECLS

G_DEFINE_AUTOPTR_CLEANUP_FUNC (DIR, closedir);

int        bolt_open (const char *path,
                      int         flags,
                      int         mode,
                      GError    **error);

gboolean   bolt_close (int      fd,
                       GError **error);

gboolean   bolt_read_all (int      fd,
                          void    *buf,
                          gsize    nbytes,
                          gsize   *nread,
                          GError **error);

gboolean   bolt_write_all (int         fd,
                           const void *buf,
                           gssize      nbytes,
                           GError    **error);

DIR *      bolt_opendir (const char *path,
                         GError    **error);

DIR *      bolt_opendir_at (int         dirfd,
                            const char *name,
                            int         oflag,
                            GError    **error);

gboolean   bolt_closedir (DIR     *d,
                          GError **error);

gboolean   bolt_rmdir (const char *name,
                       GError    **error);

int        bolt_openat (int         dirfd,
                        const char *path,
                        int         oflag,
                        GError    **error);

gboolean   bolt_unlink (const char *name,
                        GError    **error);

gboolean   bolt_unlink_at (int         dirfd,
                           const char *name,
                           int         flag,
                           GError    **error);

char *     bolt_read_value_at (int         dirfd,
                               const char *name,
                               GError    **error);

gboolean   bolt_write_char_at (int         dirfd,
                               const char *name,
                               char        value,
                               GError    **error);

gboolean   bolt_read_int_at (int         dirfd,
                             const char *name,
                             gint       *val,
                             GError    **error);

gboolean   bolt_verify_uid (int         dirfd,
                            const char *uid,
                            GError    **error);


G_END_DECLS

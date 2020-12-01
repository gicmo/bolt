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

#include <stddef.h>
#include <sys/types.h>

#include <glib.h>

G_BEGIN_DECLS

#ifndef offsetof
#define offsetof(type, member) ((size_t) &((type *) 0)->member)
#endif

#define bolt_container_of(ptr, type, member)                         \
  ({const typeof (((type *) 0)->member) * p__ = (ptr);               \
    (type *) ((void *) ((char *) p__ - offsetof (type, member))); })

/* *INDENT-OFF* */

#define bolt_swap(a, b) G_STMT_START {  \
    typeof (a) t__ = (a);               \
    (a) = (b);                          \
    (b) = t__;                          \
  } G_STMT_END

#define bolt_steal(ptr, none_value) ({  \
    typeof (*(ptr)) t__ = *(ptr);       \
    *(ptr) = (none_value);              \
    t__;                                \
    })

/* *INDENT-ON* */

#define bolt_cleanup(x) __attribute__((cleanup (x)))

#if defined(__SANITIZE_ADDRESS__)
# define HAVE_ASAN 1
#elif defined(__has_feature)
# if __has_feature (address_sanitizer)
#  define HAVE_ASAN 1
# endif
#endif
#ifndef HAVE_ASAN
# define HAVE_ASAN 0
#endif

G_END_DECLS

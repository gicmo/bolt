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
#include <string.h>

G_BEGIN_DECLS

#define ANSI_NORMAL "\x1B[0m"
#define ANSI_RED "\x1B[0;31m"
#define ANSI_GREEN "\x1B[0;32m"
#define ANSI_YELLOW "\x1B[0;33m"
#define ANSI_BLUE "\x1B[0;34m"
#define ANSI_HIGHLIGHT_BLACK "\x1B[0;1;30m"
#define ANSI_HIGHLIGHT_RED "\x1B[0;1;31m"

int              bolt_is_fancy_terminal (void);

const char *     bolt_color (const char *color);

typedef enum {
  TREE_VERTICAL,
  TREE_BRANCH,
  TREE_RIGHT,
  TREE_SPACE,

  BLACK_CIRCLE,
  WHITE_CIRCLE,

  ARROW,
  MDASH,
  ELLIPSIS,

  WARNING_SIGN,

  BOLT_GLYPH_LAST
} BoltGlyph;

const char *    bolt_glyph (BoltGlyph g);

G_END_DECLS

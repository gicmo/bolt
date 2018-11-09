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

#include "bolt-str.h"
#include "bolt-term.h"

#include <string.h>
#include <unistd.h>

int
bolt_is_fancy_terminal (void)
{
  const char *val;

  if (!isatty (STDOUT_FILENO))
    return 0;

  val = g_getenv ("TERM");
  if (val && !g_ascii_strcasecmp (val, "dumb"))
    return 0;

  return 1;
}

const char *
bolt_color (const char *color)
{
  static gint on = -1;

  if (G_UNLIKELY (on == -1))
    on = bolt_is_fancy_terminal ();

  return on ? color : "";
}

/* borrowed from systemd */
static const char *const ansi_glphys[BOLT_GLYPH_LAST] = {
  [TREE_VERTICAL]        = "| ",
  [TREE_BRANCH]          = "|-",
  [TREE_RIGHT]           = "`-",
  [TREE_SPACE]           = "  ",
  [WHITE_CIRCLE]         = "o",
  [BLACK_CIRCLE]         = "*",
  [ARROW]                = "->",
  [MDASH]                = "-",
  [ELLIPSIS]             = "...",
  [WARNING_SIGN]         = "!",
};

static const char *const utf8_glphys[BOLT_GLYPH_LAST] = {
  [TREE_VERTICAL]        = "\342\224\202 ",            /* │  */
  [TREE_BRANCH]          = "\342\224\234\342\224\200", /* ├─ */
  [TREE_RIGHT]           = "\342\224\224\342\224\200", /* └─ */
  [TREE_SPACE]           = "  ",                       /*    */
  [WHITE_CIRCLE]         = "\342\227\213",             /* ○ */
  [BLACK_CIRCLE]         = "\342\227\217",             /* ● */
  [ARROW]                = "\342\206\222",             /* → */
  [MDASH]                = "\342\200\223",             /* – */
  [ELLIPSIS]             = "\342\200\246",             /* … */
  [WARNING_SIGN]         = "\342\232\240",             /* ⚠ */
};


const char *
bolt_glyph (BoltGlyph g)
{
  static const char *const *glyph_table = NULL;

  g_return_val_if_fail (g < BOLT_GLYPH_LAST, "?");

  /* TODO: check for utf-8 support */
  if (G_UNLIKELY (glyph_table == NULL))
    {
      if (bolt_is_fancy_terminal ())
        glyph_table = utf8_glphys;
      else
        glyph_table = ansi_glphys;
    }

  return glyph_table[g];
}

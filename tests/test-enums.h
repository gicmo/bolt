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

#include "test-enum-types.h"
/**
 * BoltTestEnum
 * @BOLT_TEST_UNKNOWN: Unknown
 * @BOLT_TEST_ONE: One
 * @BOLT_TEST_TOW: Two
 * @BOLT_TEST_THRE: Three
 *
 * Test enumeration.
 */
typedef enum {

  BOLT_TEST_UNKNOWN = -1,
  BOLT_TEST_ONE,
  BOLT_TEST_TWO,
  BOLT_TEST_THREE

} BoltTestEnum;


/**
 * BoltTestFlags:
 * @BOLT_KITT_DISABLED: Flag for everthying is disabled.
 * @BOLT_KITT_ENABLED: Enabled flag.
 * @BOLT_KITT_SSPM: Super Pursuit Mode.
 * @BOLT_KITT_TURBO_BOOST: Turbo Boost.
 * @BOLT_KITT_SKI_MODE: Ski Mode.
 *
 * KITT features.
 */
typedef enum { /*< flags >*/

  BOLT_KITT_DISABLED    = 0,
  BOLT_KITT_ENABLED     = 1,
  BOLT_KITT_SSPM        = 1 << 1,
  BOLT_KITT_TURBO_BOOST = 1 << 2,
  BOLT_KITT_SKI_MODE    = 1 << 3,

  BOLT_KITT_DEFAULT     = BOLT_KITT_ENABLED | BOLT_KITT_SSPM,

} BoltKittFlags;

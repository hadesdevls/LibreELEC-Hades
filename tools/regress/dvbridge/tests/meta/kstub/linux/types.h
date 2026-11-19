/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 LibreELEC Hades
 *
 * Stand-in for the kernel's linux/types.h in the host tests of the HDR10+ headers (khost_test.c, i915_model.c,
 * kedid.c): the types they use, from the C library.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
typedef uint8_t u8;
typedef uint32_t u32;

/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#pragma once

// Enable inline implementations and advanced API (XXH3)
#ifndef XXH_INLINE_ALL
#define XXH_INLINE_ALL
#endif

// Can't use XXH_X86DISPATCH because XXH is calculated on multiple threads,
// while the dispatch writes the result (multiple pointers without any
// synchronization) to XXH_g_dispatch at the first call.

#include <xxhash.h>


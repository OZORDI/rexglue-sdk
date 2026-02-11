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

#include <rex/graphics/flags.h>

REXCVAR_DEFINE_BOOL(gpu_allow_invalid_fetch_constants, false,
    "Allow invalid fetch constants", "GPU");
REXCVAR_DEFINE_BOOL(native_2x_msaa, true,
    "Enable native 2x MSAA", "GPU");
REXCVAR_DEFINE_BOOL(depth_float24_round, false,
    "Round float24 depth values", "GPU");
REXCVAR_DEFINE_BOOL(depth_float24_convert_in_pixel_shader, false,
    "Convert float24 depth in pixel shader", "GPU");
REXCVAR_DEFINE_BOOL(depth_transfer_not_equal_test, false,
    "Use not-equal test for depth transfer", "GPU");
REXCVAR_DEFINE_STRING(dump_shaders, "",
    "Path to dump shaders to", "GPU");

REXCVAR_DEFINE_STRING(gpu, "any",
    "GPU backend: 'any' (auto-detect), 'd3d12', 'vulkan', 'metal'", "GPU");

REXCVAR_DEFINE_BOOL(metal_validation_enabled, false,
    "Enable Metal API validation layer (macOS)", "GPU");
REXCVAR_DEFINE_INT32(metal_gpu_family_override, -1,
    "Override Metal GPU family detection (-1 = auto)", "GPU");

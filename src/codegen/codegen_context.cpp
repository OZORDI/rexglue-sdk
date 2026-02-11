/**
 * @file        rexcodegen/codegen_context.cpp
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include <rex/codegen/codegen_context.h>
#include "decoded_binary.h"
#include <rex/runtime.h>
#include <rex/runtime/xex_module.h>
#include <rex/kernel/kernel_state.h>
#include <rex/kernel/user_module.h>
#include <rex/logging.h>
#include <fmt/format.h>

namespace rex::codegen {

Result<CodegenContext> CodegenContext::Create(
    const std::filesystem::path& configPath,
    Runtime& runtime)
{
    CodegenContext ctx;

    // Load configuration
    ctx.config_.Load(configPath.string());
    ctx.configDir_ = configPath.parent_path();

    // Determine XEX path
    std::filesystem::path xexPath;
    if (!ctx.config_.patchedFilePath.empty()) {
        xexPath = ctx.configDir_ / ctx.config_.patchedFilePath;
        if (!std::filesystem::exists(xexPath)) {
            REXCODEGEN_ERROR("Patched file not found: {}", xexPath.string());
            xexPath.clear();
        }
    }

    if (xexPath.empty()) {
        xexPath = ctx.configDir_ / ctx.config_.filePath;

        if (!ctx.config_.patchFilePath.empty()) {
            REXCODEGEN_INFO("Patch file specified but XexPatcher not available. Use a pre-patched XEX file instead.");
            REXCODEGEN_INFO("Ignoring patch file: {}", ctx.config_.patchFilePath);
        }
    }

    // Resolve path
    if (std::filesystem::exists(xexPath)) {
        xexPath = std::filesystem::canonical(xexPath);
    } else {
        return Err<CodegenContext>(ErrorCategory::NotFound, fmt::format("XEX file not found: {}", xexPath.string()));
    }

    // Load XEX via Runtime
    auto xexFilename = xexPath.filename();
    auto vfsPath = "game:\\" + xexFilename.string();
    fprintf(stderr, "[TRACE] CodegenContext::Create - calling LoadXexImage(%s)\n", vfsPath.c_str()); fflush(stderr);
    auto status = runtime.LoadXexImage(vfsPath);
    if (status != X_STATUS_SUCCESS) {
        return Err<CodegenContext>(ErrorCategory::Format, fmt::format("Failed to load XEX: {} (status {:#x})", xexPath.string(), status));
    }
    fprintf(stderr, "[TRACE] CodegenContext::Create - LoadXexImage returned OK\n"); fflush(stderr);
    REXCODEGEN_INFO("XEX image loaded successfully via Runtime");
    REX_LOG_FLUSH();

    // Get module and create BinaryView
    auto user_module = runtime.kernel_state()->GetExecutableModule();
    if (!user_module) {
        return Err<CodegenContext>(ErrorCategory::Format, "Failed to get executable module after loading");
    }

    auto* module = user_module->xex_module();
    if (!module) {
        return Err<CodegenContext>(ErrorCategory::Format, "Failed to get XexModule from UserModule");
    }

    fprintf(stderr, "[TRACE] CodegenContext::Create - creating BinaryView\n"); fflush(stderr);
    ctx.binary_ = BinaryView::fromModule(*module);
    ctx.resolver_ = runtime.export_resolver();

    REXCODEGEN_INFO("Loaded XEX: base=0x{:08X}, size=0x{:X}, entry=0x{:08X}",
                    ctx.binary_.baseAddress(), ctx.binary_.imageSize(), ctx.binary_.entryPoint());
    REX_LOG_FLUSH();

    fprintf(stderr, "[TRACE] CodegenContext::Create - done, base=0x%08X size=0x%X\n", ctx.binary_.baseAddress(), ctx.binary_.imageSize()); fflush(stderr);
    // Initialize AnalysisState from binary
    ctx.analysisState_.format = "xex";
    ctx.analysisState_.loadAddress = ctx.binary_.baseAddress();
    ctx.analysisState_.entryPoint = ctx.binary_.entryPoint();
    ctx.analysisState_.imageSize = ctx.binary_.imageSize();

    return ctx;
}

CodegenContext CodegenContext::Create(BinaryView binary, RecompilerConfig config) {
    CodegenContext ctx;
    ctx.binary_ = std::move(binary);
    ctx.config_ = std::move(config);
    return ctx;
}

CodegenContext::~CodegenContext() = default;
CodegenContext::CodegenContext(CodegenContext&&) = default;
CodegenContext& CodegenContext::operator=(CodegenContext&&) = default;

DecodedBinary& CodegenContext::decoded() {
    assert(decoded_ && "Call initDecoded() before accessing decoded()");
    return *decoded_;
}

const DecodedBinary& CodegenContext::decoded() const {
    assert(decoded_ && "Call initDecoded() before accessing decoded()");
    return *decoded_;
}

void CodegenContext::initDecoded() {
    decoded_ = std::make_unique<DecodedBinary>(binary_);
    decoded_->decode();
}

} // namespace rex::codegen

/**
 * @file        rexglue/main.cpp
 * @brief       ReXGlue CLI tool entry point
 *
 * @copyright   Copyright (c) 2026 Tom Clay
 * @license     BSD 3-Clause License
 */

#include "cli_utils.h"
#include "commands/codegen_command.h"
#include "commands/init_command.h"
#include "commands/test_recompiler.h"
#include <rex/logging.h>
#include <rex/result.h>

#include <absl/flags/flag.h>
#include <absl/flags/parse.h>
#include <absl/flags/usage.h>

#include <iostream>
#include <map>

// Analyze/Codegen flags
// TODO(tomc): should probably move these to codegen_command.cpp and consider using CVAR system
ABSL_FLAG(bool, force, false, "Generate output even if validation errors occur");
ABSL_FLAG(bool, no_exception_handlers, false, "Skip generation of SEH exception handler code");

// Recompile-tests flags
// TODO(tomc): should probably move these to test_recompiler.cpp and consider using CVAR system
ABSL_FLAG(std::string, bin_dir, "", "Directory containing linked .bin and .map files");
ABSL_FLAG(std::string, asm_dir, "", "Directory containing .s assembly source files");
ABSL_FLAG(std::string, output, "", "Output path for recompile-tests");

// Init flags
// TODO(tomc): .. you guessed it, should probably move these to init_command.cpp and consider using CVAR system
ABSL_FLAG(std::string, app_name, "", "Project name for init command");
ABSL_FLAG(std::string, app_root, "", "Project root directory for init command");
ABSL_FLAG(std::string, app_desc, "", "Project description (optional)");
ABSL_FLAG(std::string, app_author, "", "Project author (optional)");
ABSL_FLAG(bool, sdk_example, false, "Create as SDK example (omit vcpkg.json)");

using rex::Result;
using rex::Ok;

void PrintUsage() {
    std::cerr << "ReXGlue - Xbox 360 Recompilation Toolkit\n\n";
    std::cerr << "Usage: rexglue <command> [flags] [args]\n\n";
    std::cerr << "Commands:\n";
    std::cerr << "  codegen <config.toml>   Analyze XEX and generate C++ code\n";
    std::cerr << "  init                    Initialize a new project\n";
    std::cerr << "  recompile-tests         Generate Catch2 tests from PPC assembly\n\n";
    std::cerr << "Run 'rexglue --help' for flag details.\n";
}

int main(int argc, char** argv) {
    absl::SetProgramUsageMessage("ReXGlue - Xbox 360 Recompilation Toolkit");
    rex::cvar::Init(argc, argv);
    rex::cvar::ApplyEnvironment();

    std::vector<char*> remaining = absl::ParseCommandLine(argc, argv);
    std::string command;

    if (remaining.size() > 1) {
        command = remaining[1];
    }

    if (command.empty()) {
        PrintUsage();
        return 1;
    }

    // Set up logging from CVARs
    std::string level_str = REXCVAR_GET(log_level);
    std::string log_file_path = REXCVAR_GET(log_file);
    bool verbose = REXCVAR_GET(log_verbose);

    // Verbose overrides level if not explicitly set
    if (verbose && level_str == "info") {
        level_str = "trace";
        rex::cvar::SetFlagByName("log_level", "trace");
    }

    std::map<std::string, std::string> category_levels;
    auto log_config = rex::BuildLogConfig(
        log_file_path.empty() ? nullptr : log_file_path.c_str(),
        level_str,
        category_levels
    );
    rex::InitLogging(log_config);

    // Register callback for runtime level changes
    rex::RegisterLogLevelCallback();

    // TODO(tomc): make the version dynamic (at least, not baked into a string)
    REXLOG_INFO("ReXGlue v0.1.0 - Xbox 360 Recompilation Toolkit");

    // Set up CLI context
    rexglue::cli::CliContext ctx;
    ctx.verbose = verbose;
    ctx.force = absl::GetFlag(FLAGS_force);
    ctx.noExceptionHandlers = absl::GetFlag(FLAGS_no_exception_handlers);

    Result<void> result = Ok();
    if (command == "init") {
        rexglue::cli::InitOptions opts;
        opts.app_name = absl::GetFlag(FLAGS_app_name);
        opts.app_root = absl::GetFlag(FLAGS_app_root);
        opts.app_desc = absl::GetFlag(FLAGS_app_desc);
        opts.app_author = absl::GetFlag(FLAGS_app_author);
        opts.sdk_example = absl::GetFlag(FLAGS_sdk_example);
        opts.force = ctx.force;

        if (opts.app_name.empty()) {
            REXLOG_ERROR("--app-name is required for init command");
            return 1;
        }
        if (opts.app_root.empty()) {
            REXLOG_ERROR("--app-root is required for init command");
            return 1;
        }

        result = rexglue::cli::InitProject(opts, ctx);
    }
    else if (command == "codegen") {
        if (remaining.size() < 3) {
            REXLOG_ERROR("Missing config path. Usage: rexglue codegen <config.toml>");
            return 1;
        }
        if (remaining.size() > 3) {
            REXLOG_ERROR("Too many arguments for codegen command");
            return 1;
        }
        std::string config_path = remaining[2];
        result = rexglue::cli::CodegenFromConfig(config_path, ctx);
    }
    else if (command == "recompile-tests") {
        std::string bin_dir = absl::GetFlag(FLAGS_bin_dir);
        std::string asm_dir = absl::GetFlag(FLAGS_asm_dir);
        std::string output = absl::GetFlag(FLAGS_output);

        if (bin_dir.empty() || asm_dir.empty() || output.empty()) {
            REXLOG_ERROR("--bin-dir, --asm-dir, and --output are required");
            return 1;
        }

        if (!rexglue::commands::recompile_tests(bin_dir, asm_dir, output)) {
            REXLOG_ERROR("Test recompilation failed");
            return 1;
        }
    }
    else {
        REXLOG_ERROR("Unknown command: {}", command);
        PrintUsage();
        return 1;
    }

    if (!result) {
        REXLOG_ERROR("Operation failed: {}", result.error().what());
        return 1;
    }

    REXLOG_INFO("Operation completed successfully");
    return 0;
}

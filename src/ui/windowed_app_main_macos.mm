/**
 ******************************************************************************
 * ReXGlue SDK — macOS Windowed App Entry Point                               *
 ******************************************************************************
 * Phase 2: Native Cocoa/AppKit entry point replacing POSIX/GTK entry point   *
 * on macOS.  Initializes NSApplication and the MacOSWindowedAppContext.       *
 ******************************************************************************
 */

#import <Cocoa/Cocoa.h>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <rex/logging.h>
#include <rex/filesystem.h>
#include <rex/ui/windowed_app.h>
#include <rex/ui/windowed_app_context_macos.h>
#include <spdlog/common.h>
#include <filesystem>

namespace {

// TEMP: Replace with CVAR system
// Match positional args to registered option names
std::map<std::string, std::string> MatchPositionalArgs(
    int argc, char** argv,
    const std::vector<std::string>& option_names) {
  std::map<std::string, std::string> result;
  // Skip argv[0] (program name)
  size_t arg_count = argc > 1 ? static_cast<size_t>(argc - 1) : 0;
  size_t count = std::min(arg_count, option_names.size());
  for (size_t i = 0; i < count; ++i) {
    result[option_names[i]] = argv[i + 1];
  }
  return result;
}

}  // namespace

extern "C" int main(int argc, char** argv) {
  int result;

  @autoreleasepool {
    // Ensure NSApplication is created and set as the shared application.
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

    // Create the default main menu so standard shortcuts (Cmd-Q, Cmd-H, etc.)
    // work out of the box.
    {
      NSMenu* main_menu = [[NSMenu alloc] init];
      NSMenuItem* app_menu_item = [[NSMenuItem alloc] init];
      [main_menu addItem:app_menu_item];
      NSMenu* app_menu = [[NSMenu alloc] init];
      [app_menu addItemWithTitle:@"Quit"
                          action:@selector(terminate:)
                   keyEquivalent:@"q"];
      [app_menu_item setSubmenu:app_menu];
      [NSApp setMainMenu:main_menu];
    }

    rex::ui::MacOSWindowedAppContext app_context;

    std::unique_ptr<rex::ui::WindowedApp> app =
        rex::ui::GetWindowedAppCreator()(app_context);

    // TEMP: Replace with CVAR system - parse positional arguments
    auto parsed =
        MatchPositionalArgs(argc, argv, app->GetPositionalOptions());
    app->SetParsedArguments(std::move(parsed));

    // Initialize logging.
    std::filesystem::path exe_dir = rex::filesystem::GetExecutableFolder();
    std::filesystem::path log_path = exe_dir / (app->GetName() + ".log");

    try {
      rex::InitLogging(log_path.string().c_str());
    } catch (const spdlog::spdlog_ex& e) {
      std::fprintf(stderr, "Logging init failed for '%s': %s\n",
                   log_path.string().c_str(), e.what());
      rex::InitLogging(nullptr);
    }

    if (app->OnInitialize()) {
      app_context.RunLoop();
      result = EXIT_SUCCESS;
    } else {
      result = EXIT_FAILURE;
    }

    app->InvokeOnDestroy();
  }

  // Logging may still be needed in the destructors.
  rex::ShutdownLogging();

  return result;
}

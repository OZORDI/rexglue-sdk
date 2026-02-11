/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <cstdlib>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/platform.h>
#include <rex/ui/windowed_app.h>
#include <rex/ui/windowed_app_context_win.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(enable_console, true,
    "Enable console window on Windows",
    "UI/Window");

namespace {

// TEMP: Replace with CVAR system
// Convert wide string to UTF-8
std::string WideToUtf8(const std::wstring& wide) {
  if (wide.empty()) return {};
  int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                  static_cast<int>(wide.size()),
                                  nullptr, 0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string result(static_cast<size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                      static_cast<int>(wide.size()),
                      result.data(), size, nullptr, nullptr);
  return result;
}

// TEMP: Replace with CVAR system
// Simple command line tokenizer - splits on whitespace, handles quoted strings
std::vector<std::string> TokenizeCommandLine(const wchar_t* command_line) {
  std::vector<std::string> args;
  if (!command_line || !*command_line) {
    return args;
  }

  std::wstring current;
  bool in_quotes = false;

  for (const wchar_t* p = command_line; *p; ++p) {
    if (*p == L'"') {
      in_quotes = !in_quotes;
    } else if (*p == L' ' && !in_quotes) {
      if (!current.empty()) {
        args.push_back(WideToUtf8(current));
        current.clear();
      }
    } else {
      current += *p;
    }
  }

  if (!current.empty()) {
    args.push_back(WideToUtf8(current));
  }

  return args;
}

// TEMP: Replace with CVAR system
// Match positional args to registered option names
std::map<std::string, std::string> MatchPositionalArgs(
    const std::vector<std::string>& args,
    const std::vector<std::string>& option_names) {
  std::map<std::string, std::string> result;
  size_t count = std::min(args.size(), option_names.size());
  for (size_t i = 0; i < count; ++i) {
    result[option_names[i]] = args[i];
  }
  return result;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hinstance, HINSTANCE hinstance_prev,
                    LPWSTR command_line, int show_cmd) {
  (void)hinstance_prev;

  // Allocate a console for debugging if enabled
  if (REXCVAR_GET(enable_console)) {
    AllocConsole();
    FILE* fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);
    freopen_s(&fp, "CONIN$", "r", stdin);
    printf("Console attached for debugging\n");
  }

  int result;

  {
    rex::ui::Win32WindowedAppContext app_context(hinstance, show_cmd);
    // TODO(Triang3l): Initialize creates a window. Set DPI awareness via the
    // manifest.
    if (!app_context.Initialize()) {
      return EXIT_FAILURE;
    }

    std::unique_ptr<rex::ui::WindowedApp> app =
        rex::ui::GetWindowedAppCreator()(app_context);

    // TEMP: Replace with CVAR system - parse positional arguments
    auto args = TokenizeCommandLine(command_line);
    auto parsed = MatchPositionalArgs(args, app->GetPositionalOptions());
    app->SetParsedArguments(std::move(parsed));

    // Initialize COM on the UI thread with the apartment-threaded concurrency
    // model, so dialogs can be used.
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
      return EXIT_FAILURE;
    }

    // TODO: Port InitializeWin32App from Xenia
    // rex::InitializeWin32App(app->GetName());

    result =
        app->OnInitialize() ? app_context.RunMainMessageLoop() : EXIT_FAILURE;

    app->InvokeOnDestroy();
  }

  // TODO: Port ShutdownWin32App from Xenia
  // Logging may still be needed in the destructors.
  // rex::ShutdownWin32App();

  CoUninitialize();

  return result;
}

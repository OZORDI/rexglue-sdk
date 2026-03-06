/**
 * macOS windowed app entry point.
 * @modified Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/common.h>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/ui/windowed_app.h>
#include <rex/ui/windowed_app_context_macos.h>

extern "C" int main(int argc, char** argv) {
  // Filter out macOS/Xcode injected flags
  std::vector<std::string> filtered_args;
  filtered_args.reserve(argc);
  for (int i = 0; i < argc; ++i) {
    std::string_view arg(argv[i]);
    if (arg.rfind("-NSDocumentRevisionsDebugMode", 0) == 0 ||
        arg.rfind("-ApplePersistenceIgnoreState", 0) == 0 ||
        arg.rfind("-YES", 0) == 0 || arg == "YES") {
      continue;
    }
    filtered_args.emplace_back(argv[i]);
  }
  std::vector<char*> fargv;
  fargv.reserve(filtered_args.size());
  for (auto& s : filtered_args) fargv.push_back(const_cast<char*>(s.c_str()));
  int fargc = static_cast<int>(fargv.size());

  auto remaining = rex::cvar::Init(fargc, fargv.data());
  rex::cvar::ApplyEnvironment();

  int result;
  {
    rex::ui::MacWindowedAppContext app_context;
    std::unique_ptr<rex::ui::WindowedApp> app =
        rex::ui::GetWindowedAppCreator()(app_context);

    // Map positional args to app's expected options
    const auto& option_names = app->GetPositionalOptions();
    std::map<std::string, std::string> parsed;
    size_t count = std::min(remaining.size(), option_names.size());
    for (size_t i = 0; i < count; ++i) {
      parsed[option_names[i]] = remaining[i];
    }
    app->SetParsedArguments(std::move(parsed));

    // Initialize logging
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
      app_context.RunMainCocoaLoop();
      result = EXIT_SUCCESS;
    } else {
      REXLOG_ERROR("Failed to initialize app");
      result = EXIT_FAILURE;
    }
    app->InvokeOnDestroy();
  }
  rex::ShutdownLogging();
  return result;
}

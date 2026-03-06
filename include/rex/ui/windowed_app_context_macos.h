#pragma once
/**
 * macOS windowed application context.
 * @modified Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <mutex>

#include <rex/ui/windowed_app_context.h>

#ifdef __OBJC__
@class NSApplication;
@class NSObject;
#else
typedef struct objc_object NSApplication;
typedef struct objc_object NSObject;
#endif

namespace rex {
namespace ui {

class MacWindowedAppContext : public WindowedAppContext {
 public:
  MacWindowedAppContext();
  ~MacWindowedAppContext() override;

  // Runs the Cocoa main event loop until quit is requested.
  void RunMainCocoaLoop();

  // Processes enqueued pending functions on the UI thread.
  void ProcessPendingFunctions();

 protected:
  void NotifyUILoopOfPendingFunctions() override;
  void PlatformQuitFromUIThread() override;

 private:
  NSApplication* app_ = nullptr;
  NSObject* delegate_ = nullptr;

  std::mutex pending_functions_mutex_;
  bool pending_functions_scheduled_ = false;
  bool should_quit_ = false;
};

}  // namespace ui
}  // namespace rex

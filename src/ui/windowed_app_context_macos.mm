/**
 * macOS windowed application context.
 * @modified Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/ui/windowed_app_context_macos.h>

#import <Cocoa/Cocoa.h>

#include <rex/assert.h>
#include <rex/logging.h>

@interface RexAppDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, assign) rex::ui::MacWindowedAppContext* context;
- (instancetype)initWithContext:(rex::ui::MacWindowedAppContext*)context;
@end

@implementation RexAppDelegate
- (instancetype)initWithContext:(rex::ui::MacWindowedAppContext*)context {
  self = [super init];
  if (self) _context = context;
  return self;
}
- (void)applicationDidFinishLaunching:(NSNotification*)notification {}
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
  if (_context) _context->RequestDeferredQuit();
  return NSTerminateCancel;
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
  return YES;
}
@end

namespace rex {
namespace ui {

MacWindowedAppContext::MacWindowedAppContext() {
  app_ = [NSApplication sharedApplication];
  if (!app_) {
    REXLOG_ERROR("MacWindowedAppContext: Failed to get NSApplication");
    return;
  }
  delegate_ = [[RexAppDelegate alloc] initWithContext:this];
  [app_ setDelegate:delegate_];
  [app_ setActivationPolicy:NSApplicationActivationPolicyRegular];
  REXLOG_INFO("MacWindowedAppContext: initialized");
}

MacWindowedAppContext::~MacWindowedAppContext() {
  if (delegate_) {
    [delegate_ release];
    delegate_ = nullptr;
  }
}

void MacWindowedAppContext::NotifyUILoopOfPendingFunctions() {
  {
    std::lock_guard<std::mutex> lock(pending_functions_mutex_);
    if (pending_functions_scheduled_) return;
    pending_functions_scheduled_ = true;
  }
  dispatch_async(dispatch_get_main_queue(), ^{
    ProcessPendingFunctions();
  });
}

void MacWindowedAppContext::PlatformQuitFromUIThread() {
  should_quit_ = true;
  [app_ stop:nil];
  NSEvent* ev = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                   location:NSMakePoint(0, 0)
                              modifierFlags:0
                                  timestamp:0
                               windowNumber:0
                                    context:nil
                                    subtype:0
                                      data1:0
                                      data2:0];
  [app_ postEvent:ev atStart:YES];
}

void MacWindowedAppContext::RunMainCocoaLoop() {
  if (HasQuitFromUIThread()) return;
  REXLOG_INFO("MacWindowedAppContext: starting Cocoa main loop");
  [app_ activateIgnoringOtherApps:YES];
  [app_ run];
  if (!HasQuitFromUIThread()) QuitFromUIThread();
  REXLOG_INFO("MacWindowedAppContext: Cocoa main loop ended");
}

void MacWindowedAppContext::ProcessPendingFunctions() {
  {
    std::lock_guard<std::mutex> lock(pending_functions_mutex_);
    pending_functions_scheduled_ = false;
  }
  ExecutePendingFunctionsFromUIThread();
}

}  // namespace ui
}  // namespace rex

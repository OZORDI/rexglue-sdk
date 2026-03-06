#pragma once
/**
 * macOS window implementation.
 * @modified Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <memory>
#include <string>

#include <rex/ui/menu_item.h>
#include <rex/ui/window.h>

#ifdef __OBJC__
@class NSWindow;
@class NSView;
@class RexWindowDelegate;
@class NSMenuItem;
#else
typedef struct objc_object NSWindow;
typedef struct objc_object NSView;
typedef struct objc_object RexWindowDelegate;
typedef struct objc_object NSMenuItem;
#endif

namespace rex {
namespace ui {

class MacWindow : public Window {
  using super = Window;

 public:
  MacWindow(WindowedAppContext& app_context, const std::string_view title,
            uint32_t desired_logical_width, uint32_t desired_logical_height);
  ~MacWindow() override;

  NSWindow* ns_window() const { return window_; }
  NSView* content_view() const { return content_view_; }

  // Called from Objective-C delegate
  void OnWindowWillClose();
  void OnWindowDidResize();
  void OnWindowDidBecomeKey();
  void OnWindowDidResignKey();
  void RequestCloseFromDelegate();
  void HandleMouseEvent(void* event);
  void HandleKeyEvent(void* event, bool is_down);

 protected:
  bool OpenImpl() override;
  void RequestCloseImpl() override;
  void ApplyNewFullscreen() override;
  void ApplyNewTitle() override;
  void ApplyNewMainMenu(MenuItem* old_main_menu) override;
  void FocusImpl() override;
  std::unique_ptr<Surface> CreateSurfaceImpl(Surface::TypeFlags allowed_types) override;
  void RequestPaintImpl() override;

 private:
  void HandleSizeUpdate();
  NSMenuItem* CreateNSMenuItemFromMenuItem(MenuItem* menu_item);

  NSWindow* window_ = nullptr;
  NSView* content_view_ = nullptr;
  RexWindowDelegate* delegate_ = nullptr;
  bool is_closing_ = false;
  void* refresh_timer_ = nullptr;
};

class MacMenuItem : public MenuItem {
 public:
  MacMenuItem(Type type, const std::string& text, const std::string& hotkey,
              std::function<void()> callback);
  ~MacMenuItem() override;

  void* handle() const { return menu_item_; }
  void TriggerSelection() { OnSelected(); }

 protected:
  void OnChildAdded(MenuItem* child_item) override;
  void OnChildRemoved(MenuItem* child_item) override;

 private:
  void* menu_item_ = nullptr;
};

}  // namespace ui
}  // namespace rex

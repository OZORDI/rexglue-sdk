/**
 * macOS window implementation.
 * @modified Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <rex/ui/window_macos.h>

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

#include <rex/assert.h>
#include <rex/logging.h>
#include <rex/ui/surface_macos.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/windowed_app_context.h>

// ── Objective-C classes ───────────────────────────────────────────────────────

@interface RexWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, assign) rex::ui::MacWindow* window;
- (instancetype)initWithWindow:(rex::ui::MacWindow*)win;
@end

@interface RexContentView : NSView {
  rex::ui::MacWindow* rex_window_;
}
- (instancetype)initWithWindow:(rex::ui::MacWindow*)win;
@end

@interface RexMenuActionHandler : NSObject
- (void)menuItemSelected:(NSMenuItem*)sender;
@end

// ── RexWindowDelegate ─────────────────────────────────────────────────────────

@implementation RexWindowDelegate
- (instancetype)initWithWindow:(rex::ui::MacWindow*)win {
  self = [super init];
  if (self) _window = win;
  return self;
}
- (void)windowWillClose:(NSNotification*)n { if (_window) _window->OnWindowWillClose(); }
- (void)windowDidResize:(NSNotification*)n { if (_window) _window->OnWindowDidResize(); }
- (void)windowDidBecomeKey:(NSNotification*)n { if (_window) _window->OnWindowDidBecomeKey(); }
- (void)windowDidResignKey:(NSNotification*)n { if (_window) _window->OnWindowDidResignKey(); }
- (BOOL)windowShouldClose:(NSWindow*)sender {
  if (_window) _window->RequestCloseFromDelegate();
  return YES;
}
@end

// ── RexContentView ────────────────────────────────────────────────────────────

@implementation RexContentView
- (instancetype)initWithWindow:(rex::ui::MacWindow*)win {
  self = [super init];
  if (self) {
    rex_window_ = win;
    NSTrackingArea* ta = [[NSTrackingArea alloc]
        initWithRect:[self bounds]
             options:NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved |
                     NSTrackingActiveInKeyWindow
               owner:self
            userInfo:nil];
    [self addTrackingArea:ta];
  }
  return self;
}
- (BOOL)wantsUpdateLayer { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)becomeFirstResponder  { return YES; }

- (void)mouseDown:(NSEvent*)e      { if (rex_window_) rex_window_->HandleMouseEvent(e); }
- (void)mouseUp:(NSEvent*)e        { if (rex_window_) rex_window_->HandleMouseEvent(e); }
- (void)mouseMoved:(NSEvent*)e     { if (rex_window_) rex_window_->HandleMouseEvent(e); }
- (void)mouseDragged:(NSEvent*)e   { if (rex_window_) rex_window_->HandleMouseEvent(e); }
- (void)rightMouseDown:(NSEvent*)e { if (rex_window_) rex_window_->HandleMouseEvent(e); }
- (void)rightMouseUp:(NSEvent*)e   { if (rex_window_) rex_window_->HandleMouseEvent(e); }
- (void)scrollWheel:(NSEvent*)e    { if (rex_window_) rex_window_->HandleMouseEvent(e); }
- (void)keyDown:(NSEvent*)e        { if (rex_window_) rex_window_->HandleKeyEvent(e, true); }
- (void)keyUp:(NSEvent*)e          { if (rex_window_) rex_window_->HandleKeyEvent(e, false); }

- (void)drawRect:(NSRect)r {
  [super drawRect:r];
  if (rex_window_) rex_window_->RequestPaint();
}
@end

// ── RexMenuActionHandler ──────────────────────────────────────────────────────

@implementation RexMenuActionHandler
- (void)menuItemSelected:(NSMenuItem*)sender {
  NSValue* val = [sender representedObject];
  if (val) {
    rex::ui::MacMenuItem* item = static_cast<rex::ui::MacMenuItem*>([val pointerValue]);
    if (item) item->TriggerSelection();
  }
}
@end

static RexMenuActionHandler* g_menu_handler = nil;

// ── rex::ui namespace ─────────────────────────────────────────────────────────

namespace rex {
namespace ui {

// Factory
std::unique_ptr<Window> Window::Create(WindowedAppContext& app_context,
                                       const std::string_view title,
                                       uint32_t desired_logical_width,
                                       uint32_t desired_logical_height) {
  return std::make_unique<MacWindow>(app_context, title,
                                     desired_logical_width,
                                     desired_logical_height);
}

std::unique_ptr<MenuItem> MenuItem::Create(Type type, const std::string& text,
                                           const std::string& hotkey,
                                           std::function<void()> callback) {
  return std::make_unique<MacMenuItem>(type, text, hotkey, std::move(callback));
}

// ── MacWindow ─────────────────────────────────────────────────────────────────

MacWindow::MacWindow(WindowedAppContext& app_context, const std::string_view title,
                     uint32_t desired_logical_width, uint32_t desired_logical_height)
    : Window(app_context, title, desired_logical_width, desired_logical_height) {}

MacWindow::~MacWindow() {
  EnterDestructor();
  if (refresh_timer_) {
    [(__bridge NSTimer*)refresh_timer_ invalidate];
    refresh_timer_ = nullptr;
  }
  if (delegate_) { [delegate_ release]; delegate_ = nullptr; }
  if (window_)   { [window_ release];   window_ = nullptr; }
}

bool MacWindow::OpenImpl() {
  if (window_) return true;

  NSRect frame = NSMakeRect(100, 100, GetDesiredLogicalWidth(), GetDesiredLogicalHeight());
  window_ = [[NSWindow alloc]
      initWithContentRect:frame
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                          NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                  backing:NSBackingStoreBuffered
                    defer:NO];
  if (!window_) { REXLOG_ERROR("MacWindow: Failed to create NSWindow"); return false; }

  delegate_ = [[RexWindowDelegate alloc] initWithWindow:this];
  [window_ setDelegate:delegate_];
  [window_ setTitle:[NSString stringWithUTF8String:GetTitle().c_str()]];
  [window_ setAcceptsMouseMovedEvents:YES];

  RexContentView* view = [[RexContentView alloc] initWithWindow:this];
  [view setFrame:[[window_ contentView] frame]];

  // Set up CAMetalLayer on the view
  [view setWantsLayer:YES];
  CAMetalLayer* metal_layer = [CAMetalLayer layer];
  metal_layer.framebufferOnly = YES;
  metal_layer.opaque = YES;
  [view setLayer:metal_layer];

  [window_ setContentView:view];
  content_view_ = view;
  [window_ makeFirstResponder:view];
  [window_ makeKeyAndOrderFront:nil];

  // 60fps refresh timer
  MacWindow* self_ptr = this;
  NSTimer* timer = [NSTimer scheduledTimerWithTimeInterval:1.0 / 60.0
                                                   repeats:YES
                                                     block:^(NSTimer*) {
    if (self_ptr && self_ptr->content_view_) {
      [self_ptr->content_view_ setNeedsDisplay:YES];
      self_ptr->OnPaint();
    }
  }];
  refresh_timer_ = (__bridge void*)timer;

  if (GetMainMenu()) ApplyNewMainMenu(nullptr);

  WindowDestructionReceiver dsr(this);
  HandleSizeUpdate();
  if (dsr.IsWindowDestroyedOrClosed()) return false;

  return true;
}

void MacWindow::RequestCloseFromDelegate() {
  if (is_closing_) return;
  is_closing_ = true;
  WindowDestructionReceiver dsr(this);
  OnBeforeClose(dsr);
}

void MacWindow::RequestCloseImpl() { RequestCloseFromDelegate(); }

void MacWindow::ApplyNewFullscreen() {
  if (!window_) return;
  bool is_fs = ([window_ styleMask] & NSWindowStyleMaskFullScreen) != 0;
  if (IsFullscreen() != is_fs) [window_ toggleFullScreen:nil];
}

void MacWindow::ApplyNewTitle() {
  if (window_) [window_ setTitle:[NSString stringWithUTF8String:GetTitle().c_str()]];
}

void MacWindow::ApplyNewMainMenu(MenuItem* old_main_menu) {
  auto* main_menu = GetMainMenu();
  if (!main_menu) { [NSApp setMainMenu:nil]; return; }

  NSMenu* bar = [[NSMenu alloc] initWithTitle:@"MainMenu"];
  for (size_t i = 0; i < main_menu->child_count(); ++i) {
    NSMenuItem* item = CreateNSMenuItemFromMenuItem(main_menu->child(i));
    if (item) [bar addItem:item];
  }
  [NSApp setMainMenu:bar];
}

void MacWindow::FocusImpl() {
  if (window_) [window_ makeKeyAndOrderFront:nil];
}

std::unique_ptr<Surface> MacWindow::CreateSurfaceImpl(Surface::TypeFlags allowed_types) {
  if (!content_view_) return nullptr;
  if (allowed_types & Surface::kTypeFlag_MacOSMetalLayer) {
    CAMetalLayer* layer = (CAMetalLayer*)[content_view_ layer];
    if (layer) return std::make_unique<MacOSMetalLayerSurface>(layer);
  }
  return nullptr;
}

void MacWindow::RequestPaintImpl() {
  if (!app_context().IsInUIThread()) {
    app_context().CallInUIThread([this]() { RequestPaintImpl(); });
    return;
  }
  if (content_view_) [content_view_ setNeedsDisplay:YES];
  OnPaint();
}

void MacWindow::HandleSizeUpdate() {
  if (!window_) return;
  NSRect cr = [window_ contentRectForFrameRect:[window_ frame]];
  uint32_t w = static_cast<uint32_t>(cr.size.width);
  uint32_t h = static_cast<uint32_t>(cr.size.height);
  WindowDestructionReceiver dsr(this);
  OnActualSizeUpdate(w, h, dsr);

  // Update CAMetalLayer drawable size to match content size in pixels
  if (content_view_) {
    CAMetalLayer* layer = (CAMetalLayer*)[content_view_ layer];
    if (layer) {
      CGFloat scale = [window_ backingScaleFactor];
      layer.drawableSize = CGSizeMake(cr.size.width * scale, cr.size.height * scale);
    }
  }
}

void MacWindow::OnWindowWillClose() {
  if (is_closing_) {
    window_ = nullptr;
    content_view_ = nullptr;
    OnAfterClose();
  }
}
void MacWindow::OnWindowDidResize()   { HandleSizeUpdate(); }
void MacWindow::OnWindowDidBecomeKey() {
  WindowDestructionReceiver dsr(this);
  OnFocusUpdate(true, dsr);
}
void MacWindow::OnWindowDidResignKey() {
  WindowDestructionReceiver dsr(this);
  OnFocusUpdate(false, dsr);
}

void MacWindow::HandleMouseEvent(void* event) {
  NSEvent* ns_ev = (__bridge NSEvent*)event;
  NSPoint wl = [ns_ev locationInWindow];
  NSPoint vl = [content_view_ convertPoint:wl fromView:nil];
  NSRect bounds = [content_view_ bounds];
  int32_t x = static_cast<int32_t>(vl.x);
  int32_t y = static_cast<int32_t>(bounds.size.height - vl.y);
  x = std::max(0, std::min(x, (int32_t)bounds.size.width  - 1));
  y = std::max(0, std::min(y, (int32_t)bounds.size.height - 1));

  float sx = 0, sy = 0;
  if ([ns_ev type] == NSEventTypeScrollWheel) {
    sx = (float)[ns_ev scrollingDeltaX];
    sy = (float)[ns_ev scrollingDeltaY];
  }

  MouseEvent::Button btn = MouseEvent::Button::kNone;
  switch ([ns_ev type]) {
    case NSEventTypeLeftMouseDown: case NSEventTypeLeftMouseUp:   btn = MouseEvent::Button::kLeft;  break;
    case NSEventTypeRightMouseDown: case NSEventTypeRightMouseUp: btn = MouseEvent::Button::kRight; break;
    default: break;
  }

  MouseEvent e(this, btn, x, y, sx, sy);
  WindowDestructionReceiver dsr(this);
  switch ([ns_ev type]) {
    case NSEventTypeMouseMoved: case NSEventTypeLeftMouseDragged: case NSEventTypeRightMouseDragged:
      OnMouseMove(e, dsr); break;
    case NSEventTypeLeftMouseDown: case NSEventTypeRightMouseDown:
      OnMouseDown(e, dsr); break;
    case NSEventTypeLeftMouseUp: case NSEventTypeRightMouseUp:
      OnMouseUp(e, dsr); break;
    case NSEventTypeScrollWheel:
      OnMouseWheel(e, dsr); break;
    default: break;
  }
}

void MacWindow::HandleKeyEvent(void* event, bool is_down) {
  NSEvent* ns_ev = (__bridge NSEvent*)event;
  NSEventModifierFlags mods = [ns_ev modifierFlags];
  bool shift = (mods & NSEventModifierFlagShift) != 0;
  bool ctrl  = (mods & NSEventModifierFlagControl) != 0;
  bool alt   = (mods & NSEventModifierFlagOption) != 0;
  bool super_key = (mods & NSEventModifierFlagCommand) != 0;

  uint16_t kc = [ns_ev keyCode];
  VirtualKey vk;
  switch (kc) {
    case 48: vk = VirtualKey::kTab;     break;
    case 36: vk = VirtualKey::kReturn;  break;
    case 51: vk = VirtualKey::kBack;    break;
    case 53: vk = VirtualKey::kEscape;  break;
    case 49: vk = VirtualKey::kSpace;   break;
    case 115: vk = VirtualKey::kHome;   break;
    case 119: vk = VirtualKey::kEnd;    break;
    case 116: vk = VirtualKey::kPrior;  break;
    case 121: vk = VirtualKey::kNext;   break;
    case 117: vk = VirtualKey::kDelete; break;
    case 123: vk = VirtualKey::kLeft;   break;
    case 124: vk = VirtualKey::kRight;  break;
    case 125: vk = VirtualKey::kDown;   break;
    case 126: vk = VirtualKey::kUp;     break;
    case 56: case 60: vk = VirtualKey::kShift;   break;
    case 59: case 62: vk = VirtualKey::kControl; break;
    case 58: case 61: vk = VirtualKey::kMenu;    break;
    case 122: vk = VirtualKey::kF1;  break;
    case 120: vk = VirtualKey::kF2;  break;
    case 99:  vk = VirtualKey::kF3;  break;
    case 118: vk = VirtualKey::kF4;  break;
    case 96:  vk = VirtualKey::kF5;  break;
    case 97:  vk = VirtualKey::kF6;  break;
    case 98:  vk = VirtualKey::kF7;  break;
    case 100: vk = VirtualKey::kF8;  break;
    case 101: vk = VirtualKey::kF9;  break;
    case 109: vk = VirtualKey::kF10; break;
    case 103: vk = VirtualKey::kF11; break;
    case 111: vk = VirtualKey::kF12; break;
    case 0:  vk = VirtualKey::kA; break; case 11: vk = VirtualKey::kB; break;
    case 8:  vk = VirtualKey::kC; break; case 2:  vk = VirtualKey::kD; break;
    case 14: vk = VirtualKey::kE; break; case 3:  vk = VirtualKey::kF; break;
    case 5:  vk = VirtualKey::kG; break; case 4:  vk = VirtualKey::kH; break;
    case 34: vk = VirtualKey::kI; break; case 38: vk = VirtualKey::kJ; break;
    case 40: vk = VirtualKey::kK; break; case 37: vk = VirtualKey::kL; break;
    case 46: vk = VirtualKey::kM; break; case 45: vk = VirtualKey::kN; break;
    case 31: vk = VirtualKey::kO; break; case 35: vk = VirtualKey::kP; break;
    case 12: vk = VirtualKey::kQ; break; case 15: vk = VirtualKey::kR; break;
    case 1:  vk = VirtualKey::kS; break; case 17: vk = VirtualKey::kT; break;
    case 32: vk = VirtualKey::kU; break; case 9:  vk = VirtualKey::kV; break;
    case 13: vk = VirtualKey::kW; break; case 7:  vk = VirtualKey::kX; break;
    case 16: vk = VirtualKey::kY; break; case 6:  vk = VirtualKey::kZ; break;
    default: vk = VirtualKey(kc); break;
  }

  KeyEvent e(this, vk, 1, !is_down, shift, ctrl, alt, super_key);
  WindowDestructionReceiver dsr(this);
  if (is_down) {
    OnKeyDown(e, dsr);
    if (dsr.IsWindowDestroyedOrClosed()) return;
    if (!ctrl && !super_key) {
      NSString* chars = [ns_ev characters];
      if (chars) {
        for (NSUInteger i = 0; i < [chars length]; ++i) {
          unichar ch = [chars characterAtIndex:i];
          if (ch < 0x20 || ch == 0x7F || (ch >= 0xF700 && ch <= 0xF8FF)) continue;
          KeyEvent ce(this, VirtualKey(ch), 1, false, shift, ctrl, alt, super_key);
          OnKeyChar(ce, dsr);
          if (dsr.IsWindowDestroyedOrClosed()) return;
        }
      }
    }
  } else {
    OnKeyUp(e, dsr);
  }
}

NSMenuItem* MacWindow::CreateNSMenuItemFromMenuItem(MenuItem* menu_item) {
  if (!menu_item) return nil;
  if (!g_menu_handler) g_menu_handler = [[RexMenuActionHandler alloc] init];

  NSString* title = [[NSString stringWithUTF8String:menu_item->text().c_str()]
      stringByReplacingOccurrencesOfString:@"&" withString:@""];
  NSMenuItem* ns_item = nil;

  switch (menu_item->type()) {
    case MenuItem::Type::kNormal:
      ns_item = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
      break;
    case MenuItem::Type::kPopup: {
      ns_item = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
      NSMenu* sub = [[NSMenu alloc] initWithTitle:title];
      for (size_t i = 0; i < menu_item->child_count(); ++i) {
        NSMenuItem* child = CreateNSMenuItemFromMenuItem(menu_item->child(i));
        if (child) [sub addItem:child];
      }
      [ns_item setSubmenu:sub];
      break;
    }
    case MenuItem::Type::kString: {
      ns_item = [[NSMenuItem alloc] initWithTitle:title
                                           action:@selector(menuItemSelected:)
                                    keyEquivalent:@""];
      [ns_item setRepresentedObject:[NSValue valueWithPointer:menu_item]];
      [ns_item setTarget:g_menu_handler];
      break;
    }
    default: break;
  }
  return ns_item;
}

// ── MacMenuItem ───────────────────────────────────────────────────────────────

MacMenuItem::MacMenuItem(Type type, const std::string& text, const std::string& hotkey,
                         std::function<void()> callback)
    : MenuItem(type, text, hotkey, std::move(callback)) {}

MacMenuItem::~MacMenuItem() {}
void MacMenuItem::OnChildAdded(MenuItem*) {}
void MacMenuItem::OnChildRemoved(MenuItem*) {}

}  // namespace ui
}  // namespace rex

// The one real window: custom frame, and the show/hide lifecycle the
// responsiveness budget depends on. Routing, tabs and components arrive in
// Phase 2 — this is the container.
//
// The lifecycle rules, because they are the point of this file:
//
//   - Hide: the back buffer and every window-scoped
//     resource are released. The buffer is the one large allocation and it
//     scales with window size; keeping it while hidden is the drift G1
//     forbids.
//   - Show: a full frame is rendered offscreen BEFORE ShowWindow,
//     so the window never appears empty and then fills in.
//   - Close destroys this native window. The tray owner stays resident and
//     may create another AppWindow later. Exit belongs to the tray menu.
//
// Presentation goes through UpdateLayeredWindow: the buffer carries
// per-pixel alpha, which buys rounded corners and a soft shadow with no
// second window and no DWM trickery.
//
// This header stays free of windows.h.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "render/gdi_backend.h"

namespace render {
class GdiResourceCache;
}

namespace platform {
class SignalFanout;
class SettleTimer;
}

namespace shell {

class AppWindow final {
 public:
  // With a shared cache, fonts survive this window's close (the plan's
  // explicit choice); with none, the backend keeps a private cache.
  explicit AppWindow(render::GdiResourceCache* shared_cache = nullptr);
  ~AppWindow();

  AppWindow(const AppWindow&) = delete;
  AppWindow& operator=(const AppWindow&) = delete;

  // Content size in logical pixels; the window itself is larger by the
  // shadow margin on each side.
  bool Create(void* instance, float content_width = 960.0f, float content_height = 640.0f);

  // Renders a full frame offscreen, then shows. Idempotent while visible.
  void Show();
  // Releases the buffer and window-scoped resources; the window object
  // survives hidden.
  void Hide();
  // Destroys this native window; process lifetime belongs to the tray owner.
  void Close();
  // Real teardown.
  void Destroy();

  // Places this window beside an existing top-level window on the same
  // monitor. Right is preferred, then left; if neither side has room the
  // current placement is preserved.
  void PlaceBeside(void* anchor_window);

  bool alive() const { return hwnd_ != nullptr; }
  bool visible() const;
  void* hwnd() const { return hwnd_; }
  render::GdiBackend* backend() { return &backend_; }

  // Always-on-top pin: the left caption button toggles this. The
  // state is session-only — persistence belongs to the settings module.
  void TogglePin();
  bool pinned() const { return pinned_; }

  // Verification hooks: the lifecycle steps the message handler takes,
  // callable directly so tests do not need a message pump.
  void OnResized(int width_px, int height_px);
  void OnDpiChanged(float dpi, int suggested_width_px, int suggested_height_px);
  // Physical client coordinates -> non-client hit zone (HTCAPTION,
  // HTLEFT..., HTCLIENT, HTTRANSPARENT for the shadow margin).
  int HitTest(int x_px, int y_px) const;

  // OS broadcast signals (theme, display, colours, settings) arrive in
  // bursts; the fan-out coalesces each burst into one drain. The handler
  // receives the drained bitmask; phases that care (themes) set one here.
  void set_signal_handler(std::function<void(std::uint32_t)> handler);
  std::uint32_t last_os_signals() const { return last_os_signals_; }

  // Fires once after a burst of activity (resize, DPI change) settles.
  // The underlying timer exists only while activity is ongoing — never at
  // idle. This is the hook settle-time measurement (ETW) attaches to.
  void set_settle_handler(std::function<void()> handler);

  // Opens the settings screen (Phase 5 wires this). While no handler is
  // registered the caption's settings button does not render at all — no
  // dead UI. Registering a handler adds the button.
  void set_settings_handler(std::function<void()> handler);

  // Core windows may reuse the shell without its built-in caption controls.
  // Configure this before Create(); the main AppWindow keeps both by default.
  void set_chrome_buttons(bool show_pin, bool show_close);
  void set_close_handler(std::function<void()> handler);

  // Content integration (#71): the shell paints its frame and delegates the
  // content area to a presenter. The painter runs inside the frame scope,
  // after the chrome. Layout runs before the paint scope so text measurement
  // is legal. Pointer coordinates are relative to the content viewport below
  // the caption. Returning true means "handled, repaint". Unset hooks cost
  // nothing.
  void set_content_layout(std::function<void(const render::Rect&)> layout);
  void set_content_painter(std::function<void(render::GdiBackend&, const render::Rect&)> painter);
  void set_content_key_handler(std::function<bool(int virtual_key)> handler);
  void set_content_text_handler(std::function<bool(wchar_t character)> handler);
  void set_content_move_handler(std::function<bool(float x_logical, float y_logical)> handler);
  void set_content_down_handler(std::function<bool(float x_logical, float y_logical)> handler);
  void set_content_click_handler(std::function<bool(float x_logical, float y_logical)> handler);
  void set_content_wheel_handler(
      std::function<bool(float x_logical, float y_logical, int delta)> handler);

 private:
  static long long __stdcall WindowProc(void* window, unsigned int message,
                                        unsigned long long wparam, long long lparam);
  // `window` is the live HWND from the proc — the hwnd_ member is not set
  // yet during creation messages.
  long long HandleMessage(void* window, unsigned int message, unsigned long long wparam,
                          long long lparam);

  void RenderFullFrame();
  void PaintContent();
  render::Rect ContentViewport() const;
  int Px(float logical) const;
  float Logical(int px) const;
  enum class CaptionButton { None, Pin, Settings, Close };
  // Visible caption buttons stay left-to-right: pin, settings, close.
  // Individual core windows may omit the built-in pin and close controls.
  int ButtonAt(int x_px, int y_px) const;
  int ButtonCount() const;
  CaptionButton ButtonRole(int index) const;

  void* instance_ = nullptr;
  void* hwnd_ = nullptr;  // HWND
  render::GdiBackend backend_;
  std::unique_ptr<platform::SignalFanout> signals_;
  std::unique_ptr<platform::SettleTimer> settle_timer_;
  std::function<void(std::uint32_t)> signal_handler_;
  std::function<void()> settle_handler_;
  std::function<void()> settings_handler_;
  std::function<void()> close_handler_;
  std::function<void(const render::Rect&)> content_layout_;
  std::function<void(render::GdiBackend&, const render::Rect&)> content_painter_;
  std::function<bool(int)> content_key_handler_;
  std::function<bool(wchar_t)> content_text_handler_;
  std::function<bool(float, float)> content_move_handler_;
  std::function<bool(float, float)> content_down_handler_;
  std::function<bool(float, float)> content_click_handler_;
  std::function<bool(float, float, int)> content_wheel_handler_;
  std::uint32_t last_os_signals_ = 0;
  unsigned int drain_message_ = 0;
  float dpi_ = 96.0f;
  bool pinned_ = false;
  bool show_pin_ = true;
  bool show_close_ = true;
  int hover_button_ = -1;
  std::wstring title_ = L"dhepz";
};

}  // namespace shell

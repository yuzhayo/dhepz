#include "ui/app_window/app_window.h"

#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

#include "platform/settle_timer.h"
#include "platform/signal_fanout.h"
#include "render/gdi_resource_cache.h"
#include "resource.h"

namespace shell {
namespace {

// Logical layout at 96 DPI; everything scales with Px()/Logical().
constexpr float kShadowMargin = 24.0f;
constexpr float kCornerRadius = 10.0f;
constexpr float kCaptionHeight = 40.0f;
constexpr float kButtonWidth = 46.0f;
constexpr float kResizeBorder = 6.0f;
constexpr std::uintptr_t kSettleTimerId = 0xD3E9C1A7;
constexpr unsigned int kSettleDelayMs = 100;

// Dark palette until themes land in Phase 2.
constexpr render::Color kBackground{30, 30, 30, 255};
constexpr render::Color kBorder{90, 90, 90, 255};
constexpr render::Color kCaptionText{230, 230, 230, 255};
constexpr render::Color kHoverNeutral{255, 255, 255, 26};
constexpr render::Color kHoverClose{232, 17, 35, 255};
constexpr render::Color kShadow{0, 0, 0, 255};

// Caption icons use one Windows glyph family. Per-glyph sizes are tuned
// because MDL2 codepoints do not share the same visual bounds.
constexpr wchar_t kPinGlyphUnpinned = 0xE718;  // horizontal pin outline
constexpr wchar_t kPinGlyphPinned = 0xE840;    // diagonal pushpin
constexpr wchar_t kGearGlyph = 0xE713;         // Settings
constexpr wchar_t kCloseGlyph = 0xE8BB;        // ChromeClose

render::TextStyle IconStyle(float size_px) {
  render::TextStyle style;
  style.family = L"Segoe MDL2 Assets";
  style.size_px = size_px;
  return style;
}

constexpr float kPinUnpinnedSize = 22.0f;
constexpr float kPinPinnedSize = 19.0f;
constexpr float kGearSize = 16.0f;
constexpr float kCloseSize = 14.0f;
constexpr float kIconNudgeY = 2.0f;

}  // namespace

AppWindow::AppWindow(render::GdiResourceCache* shared_cache) : backend_(shared_cache) {}

AppWindow::~AppWindow() { Destroy(); }

bool AppWindow::Create(void* instance, float content_width, float content_height) {
  instance_ = instance;

  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.lpfnWndProc = reinterpret_cast<WNDPROC>(&AppWindow::WindowProc);
  window_class.hInstance = static_cast<HINSTANCE>(instance);
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hIcon = LoadIconW(static_cast<HINSTANCE>(instance),
                                 MAKEINTRESOURCEW(IDI_APP_ICON));
  window_class.hIconSm = LoadIconW(static_cast<HINSTANCE>(instance),
                                   MAKEINTRESOURCEW(IDI_APP_ICON));
  window_class.lpszClassName = L"dhepz.app.window";
  if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    return false;
  }

  const int width = static_cast<int>(content_width + kShadowMargin * 2);
  const int height = static_cast<int>(content_height + kShadowMargin * 2);
  hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_APPWINDOW, window_class.lpszClassName,
                          title_.c_str(),
                          WS_POPUP | WS_THICKFRAME | WS_SYSMENU,
                          CW_USEDEFAULT, CW_USEDEFAULT, width, height, nullptr, nullptr,
                          static_cast<HINSTANCE>(instance), this);
  if (hwnd_ == nullptr) {
    return false;
  }
  const HMONITOR monitor = MonitorFromWindow(static_cast<HWND>(hwnd_), MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO monitor_info{};
  monitor_info.cbSize = sizeof(monitor_info);
  if (GetMonitorInfoW(monitor, &monitor_info)) {
    const int work_width = monitor_info.rcWork.right - monitor_info.rcWork.left;
    const int work_height = monitor_info.rcWork.bottom - monitor_info.rcWork.top;
    const int x = monitor_info.rcWork.left + (work_width - width) / 2;
    const int y = monitor_info.rcWork.top + (work_height - height) / 2;
    SetWindowPos(static_cast<HWND>(hwnd_), nullptr, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  }

  drain_message_ = RegisterWindowMessageW(L"dhepz.signal.drain");
  signals_ = std::make_unique<platform::SignalFanout>(
      hwnd_, drain_message_, [this](std::uint32_t mask) {
        last_os_signals_ |= mask;
        if (signal_handler_) {
          signal_handler_(mask);
        }
      });
  settle_timer_ = std::make_unique<platform::SettleTimer>(hwnd_, kSettleTimerId, [this] {
    if (settle_handler_) {
      settle_handler_();
    }
  });
  return true;
}

void AppWindow::set_signal_handler(std::function<void(std::uint32_t)> handler) {
  signal_handler_ = std::move(handler);
}

void AppWindow::set_settle_handler(std::function<void()> handler) {
  settle_handler_ = std::move(handler);
}

void AppWindow::set_settings_handler(std::function<void()> handler) {
  settings_handler_ = std::move(handler);
  if (visible()) {
    RenderFullFrame();  // the button row gained or lost the gear
  }
}

void AppWindow::set_chrome_buttons(bool show_pin, bool show_close) {
  show_pin_ = show_pin;
  show_close_ = show_close;
}

void AppWindow::set_close_handler(std::function<void()> handler) {
  close_handler_ = std::move(handler);
}

void AppWindow::set_content_layout(std::function<void(const render::Rect&)> layout) {
  content_layout_ = std::move(layout);
}

void AppWindow::set_content_painter(
    std::function<void(render::GdiBackend&, const render::Rect&)> painter) {
  content_painter_ = std::move(painter);
}

void AppWindow::set_content_key_handler(std::function<bool(int)> handler) {
  content_key_handler_ = std::move(handler);
}

void AppWindow::set_content_text_handler(std::function<bool(wchar_t)> handler) {
  content_text_handler_ = std::move(handler);
}

void AppWindow::set_content_move_handler(std::function<bool(float, float)> handler) {
  content_move_handler_ = std::move(handler);
}

void AppWindow::set_content_down_handler(std::function<bool(float, float)> handler) {
  content_down_handler_ = std::move(handler);
}

void AppWindow::set_content_click_handler(std::function<bool(float, float)> handler) {
  content_click_handler_ = std::move(handler);
}

void AppWindow::set_content_wheel_handler(std::function<bool(float, float, int)> handler) {
  content_wheel_handler_ = std::move(handler);
}

int AppWindow::ButtonCount() const {
  return (show_pin_ ? 1 : 0) + (settings_handler_ ? 1 : 0) + (show_close_ ? 1 : 0);
}

AppWindow::CaptionButton AppWindow::ButtonRole(int index) const {
  if (index < 0) return CaptionButton::None;
  if (show_pin_) {
    if (index == 0) return CaptionButton::Pin;
    --index;
  }
  if (settings_handler_) {
    if (index == 0) return CaptionButton::Settings;
    --index;
  }
  if (show_close_ && index == 0) return CaptionButton::Close;
  return CaptionButton::None;
}

void AppWindow::Show() {
  const HWND window = static_cast<HWND>(hwnd_);
  if (window == nullptr) return;

  if (IsWindowVisible(window)) {
    SetForegroundWindow(window);
    return;
  }

  // A full frame renders offscreen BEFORE ShowWindow: the window never
  // appears empty and then fills in.
  RECT client{};
  GetClientRect(window, &client);
  OnResized(client.right, client.bottom);
  ShowWindow(window, SW_SHOW);
  SetForegroundWindow(window);
}

void AppWindow::Hide() {
  const HWND window = static_cast<HWND>(hwnd_);
  if (window == nullptr || !IsWindowVisible(window)) return;
  ShowWindow(window, SW_HIDE);
  // The buffer is the one large allocation and scales with window size.
  backend_.ReleaseSurface();
}

void AppWindow::Close() {
  if (close_handler_) {
    close_handler_();
  } else {
    Destroy();
  }
}

void AppWindow::TogglePin() {
  if (hwnd_ == nullptr) return;
  pinned_ = !pinned_;
  SetWindowPos(static_cast<HWND>(hwnd_), pinned_ ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  if (visible()) {
    RenderFullFrame();  // the glyph state changed
  }
}

void AppWindow::Destroy() {
  if (hwnd_ != nullptr) {
    DestroyWindow(static_cast<HWND>(hwnd_));
    hwnd_ = nullptr;
  }
  backend_.ReleaseSurface();
}

void AppWindow::PlaceBeside(void* anchor_window) {
  const HWND window = static_cast<HWND>(hwnd_);
  const HWND anchor = static_cast<HWND>(anchor_window);
  if (window == nullptr || anchor == nullptr) return;

  RECT anchor_rect{};
  RECT window_rect{};
  if (!GetWindowRect(anchor, &anchor_rect) || !GetWindowRect(window, &window_rect)) return;

  MONITORINFO monitor_info{};
  monitor_info.cbSize = sizeof(monitor_info);
  const HMONITOR monitor = MonitorFromWindow(anchor, MONITOR_DEFAULTTONEAREST);
  if (!GetMonitorInfoW(monitor, &monitor_info)) return;

  const int width = window_rect.right - window_rect.left;
  const int height = window_rect.bottom - window_rect.top;
  const int gap = Px(8.0f);
  int x = window_rect.left;
  if (anchor_rect.right + gap + width <= monitor_info.rcWork.right) {
    x = anchor_rect.right + gap;
  } else if (anchor_rect.left - gap - width >= monitor_info.rcWork.left) {
    x = anchor_rect.left - gap - width;
  } else {
    return;
  }
  const int work_top = static_cast<int>(monitor_info.rcWork.top);
  const int work_bottom = static_cast<int>(monitor_info.rcWork.bottom);
  const int anchor_top = static_cast<int>(anchor_rect.top);
  const int max_y = std::max(work_top, work_bottom - height);
  const int y = std::clamp(anchor_top, work_top, max_y);
  SetWindowPos(window, nullptr, x, y, 0, 0,
               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

bool AppWindow::visible() const {
  return hwnd_ != nullptr && IsWindowVisible(static_cast<HWND>(hwnd_));
}

int AppWindow::Px(float logical) const {
  return static_cast<int>(std::lround(logical * dpi_ / 96.0f));
}

float AppWindow::Logical(int px) const { return px * 96.0f / dpi_; }

render::Rect AppWindow::ContentViewport() const {
  const render::Size surface = backend_.surface_size();
  return {kShadowMargin, kShadowMargin + kCaptionHeight,
          std::max(0.0f, surface.width - kShadowMargin * 2.0f),
          std::max(0.0f, surface.height - kShadowMargin * 2.0f - kCaptionHeight)};
}

void AppWindow::OnResized(int width_px, int height_px) {
  if (width_px <= 0 || height_px <= 0) return;
  backend_.Resize({Logical(width_px), Logical(height_px)});
  RenderFullFrame();
  if (settle_timer_) {
    settle_timer_->Arm(kSettleDelayMs);  // activity: the timer exists now
  }
}

void AppWindow::OnDpiChanged(float dpi, int suggested_width_px, int suggested_height_px) {
  if (dpi <= 0.0f || hwnd_ == nullptr) return;
  dpi_ = dpi;
  // Epoch bump inside: fonts re-resolve at the new physical heights.
  backend_.SetDpi(dpi);
  SetWindowPos(static_cast<HWND>(hwnd_), nullptr, 0, 0, suggested_width_px, suggested_height_px,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  OnResized(suggested_width_px, suggested_height_px);  // arms the settle timer
}

int AppWindow::ButtonAt(int x_px, int y_px) const {
  if (hwnd_ == nullptr) return -1;
  RECT client{};
  GetClientRect(static_cast<HWND>(hwnd_), &client);
  const int margin = Px(kShadowMargin);
  const int content_left = margin;
  const int content_right = client.right - margin;
  const int caption_bottom = margin + Px(kCaptionHeight);
  const int button_width = Px(kButtonWidth);
  if (y_px < margin || y_px >= caption_bottom || x_px < content_left || x_px >= content_right) {
    return -1;
  }
  const int from_right = content_right - x_px;
  if (from_right < 0) return -1;
  const int count = ButtonCount();
  const int index = (count - 1) - from_right / button_width;  // left-to-right
  if (index < 0 || index >= count) return -1;
  return index;
}

int AppWindow::HitTest(int x_px, int y_px) const {
  if (hwnd_ == nullptr) return HTNOWHERE;
  RECT client{};
  GetClientRect(static_cast<HWND>(hwnd_), &client);

  const int margin = Px(kShadowMargin);
  const int content_left = margin;
  const int content_top = margin;
  const int content_right = client.right - margin;
  const int content_bottom = client.bottom - margin;

  // Shadow margin: clicks pass straight through.
  if (x_px < content_left || x_px >= content_right || y_px < content_top ||
      y_px >= content_bottom) {
    return HTTRANSPARENT;
  }

  const int border = std::max(6, Px(kResizeBorder));
  const bool left = x_px < content_left + border;
  const bool right = x_px >= content_right - border;
  const bool top = y_px < content_top + border;
  const bool bottom = y_px >= content_bottom - border;

  if (top && left) return HTTOPLEFT;
  if (top && right) return HTTOPRIGHT;
  if (bottom && left) return HTBOTTOMLEFT;
  if (bottom && right) return HTBOTTOMRIGHT;
  if (left) return HTLEFT;
  if (right) return HTRIGHT;
  if (top) return HTTOP;
  if (bottom) return HTBOTTOM;

  if (ButtonAt(x_px, y_px) >= 0) return HTCLIENT;
  if (y_px < content_top + Px(kCaptionHeight)) return HTCAPTION;
  return HTCLIENT;
}

void AppWindow::RenderFullFrame() {
  if (hwnd_ == nullptr || backend_.buffer_width() <= 0) return;

  // Warm the fonts OUTSIDE the frame: creation inside a paint scope is
  // refused by design.
  if (show_pin_) {
    backend_.MeasureText(std::wstring(1, kPinGlyphUnpinned),
                         IconStyle(kPinUnpinnedSize), 0.0f);
    backend_.MeasureText(std::wstring(1, kPinGlyphPinned),
                         IconStyle(kPinPinnedSize), 0.0f);
  }
  if (show_close_) {
    backend_.MeasureText(std::wstring(1, kCloseGlyph), IconStyle(kCloseSize), 0.0f);
  }
  if (settings_handler_) {
    backend_.MeasureText(std::wstring(1, kGearGlyph), IconStyle(kGearSize), 0.0f);
  }
  const render::Rect viewport = ContentViewport();
  if (content_layout_) content_layout_(viewport);

  render::Rect dirty{};
  if (!backend_.TakeInvalidation(dirty)) {
    backend_.InvalidateAll();
    backend_.TakeInvalidation(dirty);
  }
  backend_.BeginFrame(dirty);
  PaintContent();
  backend_.EndFrame();
  backend_.PresentLayered(hwnd_);
}

void AppWindow::PaintContent() {
  backend_.ClearTransparent();

  const float width = backend_.surface_size().width;
  const float height = backend_.surface_size().height;
  const float margin = kShadowMargin;
  const float radius = kCornerRadius;
  const render::Rect content{margin, margin, width - margin * 2, height - margin * 2};

  // Soft shadow: three rounded bands under the content, lightest and
  // largest first. Alpha-blended over the transparent buffer.
  const float spreads[3] = {kShadowMargin, kShadowMargin * 2.0f / 3.0f,
                            kShadowMargin / 3.0f};
  const std::uint8_t alphas[3] = {22, 40, 62};
  for (int i = 0; i < 3; ++i) {
    const float spread = spreads[i];
    render::Color shadow = kShadow;
    shadow.a = alphas[i];
    backend_.FillRoundedRect(
        {content.x - spread, content.y - spread, content.width + spread * 2,
         content.height + spread * 2},
        render::CornerRadius::Uniform(radius + spread), shadow);
  }

  backend_.FillRoundedRect(content, render::CornerRadius::Uniform(radius), kBackground);
  backend_.StrokeRoundedRect(content, render::CornerRadius::Uniform(radius), kBorder, 1.0f);

  // Caption buttons sit on the right. The window title remains available to
  // the taskbar and Alt-Tab but is not painted over the empty P2 container.
  const render::Rect caption{content.x, content.y, content.width, kCaptionHeight};
  const int count = ButtonCount();
  for (int i = 0; i < count; ++i) {
    const CaptionButton role = ButtonRole(i);
    const bool is_pin = role == CaptionButton::Pin;
    const bool is_close = role == CaptionButton::Close;
    const render::Rect button{caption.right() - (count - i) * kButtonWidth, caption.y,
                              kButtonWidth, kCaptionHeight};
    if (hover_button_ == i) {
      backend_.FillRect(button, is_close ? kHoverClose : kHoverNeutral);
    }
    const wchar_t glyph = is_pin ? (pinned_ ? kPinGlyphPinned : kPinGlyphUnpinned)
                                 : (is_close ? kCloseGlyph : kGearGlyph);
    const float size = is_pin ? (pinned_ ? kPinPinnedSize : kPinUnpinnedSize)
                              : (is_close ? kCloseSize : kGearSize);
    backend_.DrawTextRun(std::wstring(1, glyph),
                         {button.x, button.y + kIconNudgeY, button.width, button.height},
                         IconStyle(size), kCaptionText, render::TextAlign::Center,
                         render::VerticalAlign::Middle);
  }

  if (content_painter_) {
    content_painter_(backend_, ContentViewport());
  }
}

long long __stdcall AppWindow::WindowProc(void* window, unsigned int message,
                                          unsigned long long wparam, long long lparam) {
  HWND hwnd = static_cast<HWND>(window);
  AppWindow* self = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) {
    const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
    self = static_cast<AppWindow*>(create->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }
  if (self != nullptr) {
    return self->HandleMessage(hwnd, message, wparam, lparam);
  }
  return DefWindowProcW(hwnd, message, static_cast<WPARAM>(wparam), static_cast<LPARAM>(lparam));
}

long long AppWindow::HandleMessage(void* window_handle, unsigned int message,
                                   unsigned long long wparam, long long lparam) {
  HWND window = static_cast<HWND>(window_handle);
  if (message == drain_message_ && signals_) {
    signals_->DrainMessage();
    return 0;
  }
  if (message == WM_TIMER && wparam == kSettleTimerId && settle_timer_) {
    settle_timer_->OnTimer();
    return 0;
  }
  switch (message) {
    case WM_THEMECHANGED:
    case WM_DWMCOLORIZATIONCOLORCHANGED:
      if (signals_) signals_->Raise(platform::OsSignal::Theme);
      return 0;
    case WM_SYSCOLORCHANGE:
      if (signals_) signals_->Raise(platform::OsSignal::SystemColors);
      return 0;
    case WM_SETTINGCHANGE:
      if (signals_) signals_->Raise(platform::OsSignal::Settings);
      return 0;
    case WM_DISPLAYCHANGE:
      if (signals_) signals_->Raise(platform::OsSignal::Display);
      return 0;
    case WM_NCHITTEST: {
      POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      ScreenToClient(window, &point);
      return HitTest(point.x, point.y);
    }
    case WM_ERASEBKGND:
      return render::GdiBackend::EraseBackgroundResult();
    case WM_NCCALCSIZE:
      // The frame is fully custom: the client area is the whole window, so
      // layout, shadows and hit zones all work in one coordinate space.
      // WS_THICKFRAME stays for AeroSnap; the resize hit zones come from
      // WM_NCHITTEST below.
      return 0;
    case WM_SIZE: {
      // The buffer exists only while visible: Show() renders it before
      // ShowWindow, Hide() releases it. Sizes arriving while hidden (window
      // creation, hide transitions) are ignored on purpose.
      if (!IsWindowVisible(window)) return 0;
      OnResized(LOWORD(lparam), HIWORD(lparam));
      return 0;
    }
    case WM_DPICHANGED: {
      const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
      OnDpiChanged(static_cast<float>(HIWORD(wparam)), suggested->right - suggested->left,
                   suggested->bottom - suggested->top);
      return 0;
    }
    case WM_MOUSEMOVE: {
      const int x_px = GET_X_LPARAM(lparam);
      const int y_px = GET_Y_LPARAM(lparam);
      const int button = ButtonAt(x_px, y_px);
      bool repaint = false;
      if (button != hover_button_) {
        hover_button_ = button;
        repaint = true;
      }
      if (content_move_handler_) {
        const render::Rect viewport = ContentViewport();
        const float x = Logical(x_px) - viewport.x;
        const float y = Logical(y_px) - viewport.y;
        repaint = content_move_handler_(x >= 0.0f && y >= 0.0f && x < viewport.width &&
                                                y < viewport.height
                                            ? x
                                            : -1.0f,
                                        x >= 0.0f && y >= 0.0f && x < viewport.width &&
                                                y < viewport.height
                                            ? y
                                            : -1.0f) ||
                  repaint;
      }
      if (repaint && visible()) {
        RenderFullFrame();
      }
      TRACKMOUSEEVENT tracking{};
      tracking.cbSize = sizeof(tracking);
      tracking.dwFlags = TME_LEAVE;
      tracking.hwndTrack = window;
      TrackMouseEvent(&tracking);
      return 0;
    }
    case WM_MOUSELEAVE: {
      bool repaint = hover_button_ != -1;
      hover_button_ = -1;
      if (content_move_handler_) {
        repaint = content_move_handler_(-1.0f, -1.0f) || repaint;
      }
      if (repaint && visible()) {
        RenderFullFrame();
      }
      return 0;
    }
    case WM_LBUTTONDOWN: {
      if (ButtonAt(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)) >= 0) return 0;
      if (content_down_handler_) {
        const render::Rect viewport = ContentViewport();
        const float x = Logical(GET_X_LPARAM(lparam)) - viewport.x;
        const float y = Logical(GET_Y_LPARAM(lparam)) - viewport.y;
        if (x >= 0.0f && y >= 0.0f && x < viewport.width && y < viewport.height &&
            content_down_handler_(x, y)) {
          SetCapture(window);
          RenderFullFrame();
        }
      }
      return 0;
    }
    case WM_LBUTTONUP: {
      if (GetCapture() == window) ReleaseCapture();
      const int button = ButtonAt(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
      if (button < 0) {
        if (content_click_handler_) {
          const render::Rect viewport = ContentViewport();
          const float x = Logical(GET_X_LPARAM(lparam)) - viewport.x;
          const float y = Logical(GET_Y_LPARAM(lparam)) - viewport.y;
          const bool inside = x >= 0.0f && y >= 0.0f && x < viewport.width &&
                              y < viewport.height;
          if (content_click_handler_(inside ? x : -1.0f, inside ? y : -1.0f)) {
            RenderFullFrame();
          }
        }
        return 0;
      }
      switch (ButtonRole(button)) {
        case CaptionButton::Pin:
          TogglePin();
          break;
        case CaptionButton::Settings:
          settings_handler_();
          break;
        case CaptionButton::Close:
          Close();
          break;
        case CaptionButton::None:
          break;
      }
      return 0;
    }
    case WM_KEYDOWN: {
      if (content_key_handler_ && content_key_handler_(static_cast<int>(wparam))) {
        RenderFullFrame();
        return 0;
      }
      return DefWindowProcW(window, message, static_cast<WPARAM>(wparam),
                            static_cast<LPARAM>(lparam));
    }
    case WM_CHAR:
      if (content_text_handler_ &&
          content_text_handler_(static_cast<wchar_t>(wparam))) {
        RenderFullFrame();
        return 0;
      }
      return DefWindowProcW(window, message, static_cast<WPARAM>(wparam),
                            static_cast<LPARAM>(lparam));
    case WM_MOUSEWHEEL:
      if (content_wheel_handler_) {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(window, &point);
        const render::Rect viewport = ContentViewport();
        const float x = Logical(point.x) - viewport.x;
        const float y = Logical(point.y) - viewport.y;
        if (x >= 0.0f && y >= 0.0f && x < viewport.width && y < viewport.height &&
            content_wheel_handler_(x, y, GET_WHEEL_DELTA_WPARAM(wparam))) {
          RenderFullFrame();
          return 0;
        }
      }
      return DefWindowProcW(window, message, static_cast<WPARAM>(wparam),
                            static_cast<LPARAM>(lparam));
    case WM_GETMINMAXINFO: {
      const LRESULT result =
          DefWindowProcW(window, message, static_cast<WPARAM>(wparam), static_cast<LPARAM>(lparam));
      auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
      const int margin = Px(kShadowMargin) * 2;
      info->ptMinTrackSize.x = Px(320.0f) + margin;
      info->ptMinTrackSize.y = Px(200.0f) + margin;
      return result;
    }
    case WM_CLOSE:
      Close();  // destroys this window; the tray owner remains resident
      return 0;
    case WM_DESTROY:
      hwnd_ = nullptr;
      return 0;
    default:
      return DefWindowProcW(window, message, static_cast<WPARAM>(wparam),
                            static_cast<LPARAM>(lparam));
  }
}

}  // namespace shell

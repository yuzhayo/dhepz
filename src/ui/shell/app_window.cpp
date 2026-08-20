#include "ui/shell/app_window.h"

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

render::TextStyle CaptionStyle() {
  render::TextStyle style;
  style.family = L"Segoe UI";
  style.size_px = 13.0f;
  style.weight = render::FontWeight::Semibold;
  return style;
}

render::TextStyle GlyphStyle() {
  render::TextStyle style;
  style.family = L"Marlett";
  style.size_px = 11.0f;
  return style;
}

// Verified by rendering the font on the target machine; the pair is the
// user's visual choice: unpinned is the horizontal pin outline (E718),
// pinned the diagonal pushpin (E840). E713 is the settings gear.
constexpr wchar_t kPinGlyphUnpinned = 0xE718;  // horizontal pin outline
constexpr wchar_t kPinGlyphPinned = 0xE840;    // diagonal pushpin
constexpr wchar_t kGearGlyph = 0xE713;         // Settings

render::TextStyle IconStyle() {
  render::TextStyle style;
  style.family = L"Segoe MDL2 Assets";
  style.size_px = 14.0f;
  return style;
}

// Marlett and Segoe MDL2 Assets centre their glyphs differently; the nudges
// put both families on one visual row (verified against a rendered strip).
constexpr float kIconNudgeY = 2.0f;
constexpr float kMarlettNudgeY = -2.0f;

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
                          WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
                          CW_USEDEFAULT, CW_USEDEFAULT, width, height, nullptr, nullptr,
                          static_cast<HINSTANCE>(instance), this);
  if (hwnd_ == nullptr) {
    return false;
  }
  restored_width_px_ = width;
  restored_height_px_ = height;

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

int AppWindow::ButtonCount() const {
  // Left-to-right: pin, [settings], min, max/restore, close. The gear only
  // exists while something can open — no dead button.
  return settings_handler_ ? 5 : 4;
}

void AppWindow::Show() {
  const HWND window = static_cast<HWND>(hwnd_);
  if (window == nullptr || IsWindowVisible(window)) return;

  // A full frame renders offscreen BEFORE ShowWindow: the window never
  // appears empty and then fills in.
  RECT client{};
  GetClientRect(window, &client);
  OnResized(client.right, client.bottom);
  ShowWindow(window, SW_SHOW);
}

void AppWindow::Hide() {
  const HWND window = static_cast<HWND>(hwnd_);
  if (window == nullptr || !IsWindowVisible(window)) return;
  ShowWindow(window, SW_HIDE);
  // The buffer is the one large allocation and scales with window size.
  backend_.ReleaseSurface();
}

void AppWindow::Close() {
  // Resident semantics: closing the window returns to the tray, it does not
  // exit the process. Exit belongs to the tray menu.
  Hide();
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
}

bool AppWindow::visible() const {
  return hwnd_ != nullptr && IsWindowVisible(static_cast<HWND>(hwnd_));
}

int AppWindow::Px(float logical) const {
  return static_cast<int>(std::lround(logical * dpi_ / 96.0f));
}

float AppWindow::Logical(int px) const { return px * 96.0f / dpi_; }

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
  const int margin = maximized_ ? 0 : Px(kShadowMargin);
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

  const int margin = maximized_ ? 0 : Px(kShadowMargin);
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

  if (!maximized_) {
    if (top && left) return HTTOPLEFT;
    if (top && right) return HTTOPRIGHT;
    if (bottom && left) return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;
  }

  if (ButtonAt(x_px, y_px) >= 0) return HTCLIENT;
  if (y_px < content_top + Px(kCaptionHeight)) return HTCAPTION;
  return HTCLIENT;
}

void AppWindow::RenderFullFrame() {
  if (hwnd_ == nullptr || backend_.buffer_width() <= 0) return;

  // Warm the fonts OUTSIDE the frame: creation inside a paint scope is
  // refused by design.
  const render::TextStyle caption = CaptionStyle();
  const render::TextStyle glyph = GlyphStyle();
  const render::TextStyle icon = IconStyle();
  backend_.MeasureText(title_, caption, 0.0f);
  backend_.MeasureText(L"0", glyph, 0.0f);
  backend_.MeasureText(std::wstring(1, kPinGlyphUnpinned), icon, 0.0f);
  backend_.MeasureText(std::wstring(1, kPinGlyphPinned), icon, 0.0f);
  if (settings_handler_) {
    backend_.MeasureText(std::wstring(1, kGearGlyph), icon, 0.0f);
  }

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
  const float margin = maximized_ ? 0.0f : kShadowMargin;
  const float radius = maximized_ ? 0.0f : kCornerRadius;
  const render::Rect content{margin, margin, width - margin * 2, height - margin * 2};

  // Soft shadow: three rounded bands under the content, lightest and
  // largest first. Alpha-blended over the transparent buffer.
  if (!maximized_) {
    const float spreads[3] = {kShadowMargin, kShadowMargin * 2.0f / 3.0f, kShadowMargin / 3.0f};
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
  }

  backend_.FillRoundedRect(content, render::CornerRadius::Uniform(radius), kBackground);
  backend_.StrokeRoundedRect(content, render::CornerRadius::Uniform(radius), kBorder, 1.0f);

  // Caption: title left, window buttons right.
  const render::Rect caption{content.x, content.y, content.width, kCaptionHeight};
  const int count = ButtonCount();
  const float buttons_width = kButtonWidth * count;
  const float title_width = content.width - buttons_width - 28.0f;
  if (title_width > 0.0f) {
    backend_.DrawTextRun(title_, {caption.x + 14.0f, caption.y, title_width, caption.height},
                         CaptionStyle(), kCaptionText, render::TextAlign::Left,
                         render::VerticalAlign::Middle);
  }

  const render::TextStyle glyph = GlyphStyle();
  const render::TextStyle icon = IconStyle();
  const wchar_t* marlett[3] = {L"0", maximized_ ? L"2" : L"1", L"r"};  // min, max, close
  for (int i = 0; i < count; ++i) {
    // Role by distance from the right edge: 0 close, 1 max, 2 min, then the
    // left slots: settings (only with a handler) and pin leftmost.
    const int role = (count - 1) - i;
    const bool is_pin = role == 4 || (role == 3 && !settings_handler_);
    const bool is_gear = role == 3 && settings_handler_;
    const render::Rect button{caption.right() - (count - i) * kButtonWidth, caption.y,
                              kButtonWidth, kCaptionHeight};
    if (hover_button_ == i) {
      backend_.FillRect(button, role == 0 ? kHoverClose : kHoverNeutral);
    }
    if (is_pin) {
      backend_.DrawTextRun(std::wstring(1, pinned_ ? kPinGlyphPinned : kPinGlyphUnpinned),
                           {button.x, button.y + kIconNudgeY, button.width, button.height}, icon,
                           kCaptionText, render::TextAlign::Center,
                           render::VerticalAlign::Middle);
    } else if (is_gear) {
      backend_.DrawTextRun(std::wstring(1, kGearGlyph),
                           {button.x, button.y + kIconNudgeY, button.width, button.height}, icon,
                           kCaptionText, render::TextAlign::Center,
                           render::VerticalAlign::Middle);
    } else {
      backend_.DrawTextRun(marlett[2 - role],
                           {button.x, button.y + kMarlettNudgeY, button.width, button.height},
                           glyph, kCaptionText, render::TextAlign::Center,
                           render::VerticalAlign::Middle);
    }
  }
}

void AppWindow::SetMaximized(bool maximize) {
  if (hwnd_ == nullptr || maximize == maximized_) return;
  const HWND window = static_cast<HWND>(hwnd_);
  maximized_ = maximize;
  if (maximize) {
    RECT client{};
    GetClientRect(window, &client);
    restored_width_px_ = client.right;
    restored_height_px_ = client.bottom;
    const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info)) {
      SetWindowPos(window, nullptr, info.rcWork.left, info.rcWork.top,
                   info.rcWork.right - info.rcWork.left, info.rcWork.bottom - info.rcWork.top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    }
  } else {
    SetWindowPos(window, nullptr, 0, 0, restored_width_px_, restored_height_px_,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
  RECT client{};
  GetClientRect(window, &client);
  OnResized(client.right, client.bottom);
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
      if (IsIconic(window)) {
        // Minimised is hidden for budget purposes: the buffer goes away too.
        backend_.ReleaseSurface();
        return 0;
      }
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
      const int button = ButtonAt(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
      if (button != hover_button_) {
        hover_button_ = button;
        if (visible()) {
          RenderFullFrame();
        }
      }
      TRACKMOUSEEVENT tracking{};
      tracking.cbSize = sizeof(tracking);
      tracking.dwFlags = TME_LEAVE;
      tracking.hwndTrack = window;
      TrackMouseEvent(&tracking);
      return 0;
    }
    case WM_MOUSELEAVE:
      if (hover_button_ != -1) {
        hover_button_ = -1;
        if (visible()) {
          RenderFullFrame();
        }
      }
      return 0;
    case WM_LBUTTONUP: {
      const int button = ButtonAt(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
      if (button < 0) return 0;
      const int role = (ButtonCount() - 1) - button;  // 0 close … leftmost pin
      const bool is_pin = role == 4 || (role == 3 && !settings_handler_);
      if (role == 0) {
        Close();
      } else if (role == 1) {
        SetMaximized(!maximized_);
      } else if (role == 2) {
        ShowWindow(window, SW_MINIMIZE);
      } else if (is_pin) {
        TogglePin();
      } else if (role == 3 && settings_handler_) {
        settings_handler_();
      }
      return 0;
    }
    case WM_GETMINMAXINFO: {
      const LRESULT result =
          DefWindowProcW(window, message, static_cast<WPARAM>(wparam), static_cast<LPARAM>(lparam));
      auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
      const int margin = maximized_ ? 0 : Px(kShadowMargin) * 2;
      info->ptMinTrackSize.x = Px(320.0f) + margin;
      info->ptMinTrackSize.y = Px(200.0f) + margin;
      return result;
    }
    case WM_CLOSE:
      Close();  // resident: hide, never exit
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

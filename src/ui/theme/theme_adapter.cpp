#include "ui/theme/theme_adapter.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/base.h>

namespace ui::theme {
namespace {

OsTheme ThemeFromForegroundColor(const winrt::Windows::UI::Color& color) noexcept {
  // The documented heuristic from the old build: a dark foreground means
  // the apps theme is dark.
  const int luminance =
      2 * static_cast<int>(color.R) + 3 * static_cast<int>(color.G) + static_cast<int>(color.B);
  return luminance < 3 * 128 ? OsTheme::Dark : OsTheme::Light;
}

// Shared between the adapter and the WinRT callback so an in-flight change
// event can never touch freed state during teardown.
struct QueueState {
  std::mutex mutex;
  ThemeSnapshot snapshot = ReadInitialSnapshot();
  std::optional<ThemeSnapshot> queued;
};

}  // namespace

ThemeSnapshot ReadInitialSnapshot() noexcept {
  ThemeSnapshot snapshot;
  HIGHCONTRASTW contrast{};
  contrast.cbSize = sizeof(contrast);
  if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, 0, &contrast, 0) != 0) {
    snapshot.high_contrast = (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
  }
  DWORD light = 1;
  DWORD size = sizeof(light);
  const LSTATUS status = RegGetValueW(
      HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
      L"AppsUseLightTheme", RRF_RT_DWORD, nullptr, &light, &size);
  snapshot.app_theme = (status == ERROR_SUCCESS && light == 0) ? OsTheme::Dark : OsTheme::Light;
  return snapshot;
}

struct ThemeAdapter::Impl {
  std::shared_ptr<QueueState> state = std::make_shared<QueueState>();

  std::atomic<bool> stop{false};
  HANDLE stop_event = nullptr;
  std::thread monitor;
  bool monitoring = false;

  ~Impl() { Stop(); }

  void Stop() {
    if (stop_event == nullptr) return;
    stop.store(true);
    SetEvent(stop_event);
    if (monitor.joinable()) {
      monitor.join();
    }
    CloseHandle(stop_event);
    stop_event = nullptr;
    monitoring = false;
  }
};

ThemeAdapter::ThemeAdapter() : impl_(std::make_unique<Impl>()) {}

ThemeAdapter::~ThemeAdapter() = default;

std::wstring ThemeAdapter::Select(ThemePreference preference) const {
  switch (preference) {
    case ThemePreference::Dark:
      return L"dark";
    case ThemePreference::Light:
      return L"light";
    case ThemePreference::System:
      return impl_->state->snapshot.app_theme == OsTheme::Dark ? L"dark" : L"light";
  }
  return L"light";
}

const ThemeSnapshot& ThemeAdapter::snapshot() const { return impl_->state->snapshot; }

core::Status ThemeAdapter::StartMonitoring(void* window, unsigned int signal_message,
                                           std::wstring* diagnostic) {
  if (window == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument, L"StartMonitoring requires a window");
  }
  if (impl_->monitoring) {
    return core::Ok();
  }
  const HWND target = static_cast<HWND>(window);

  impl_->stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (impl_->stop_event == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::Internal, L"Stop event creation failed");
  }
  const HANDLE stop_event = impl_->stop_event;
  const std::shared_ptr<QueueState> state = impl_->state;

  // The only thread this file spawns. It parks on the stop event; WinRT
  // change callbacks run on WinRT pool threads and merely queue a snapshot
  // plus post the signal — the UI thread does the work in its drain.
  impl_->monitor = std::thread([target, signal_message, stop_event, state] {
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    bool subscribed = false;
    winrt::event_token token{};
    winrt::Windows::UI::ViewManagement::UISettings settings{nullptr};
    try {
      settings = winrt::Windows::UI::ViewManagement::UISettings();
      token = settings.ColorValuesChanged(
          [target, signal_message, state](
              const winrt::Windows::UI::ViewManagement::UISettings& sender,
              const winrt::Windows::Foundation::IInspectable&) {
            ThemeSnapshot fresh;
            {
              std::lock_guard lock(state->mutex);
              fresh.high_contrast = state->snapshot.high_contrast;
            }
            fresh.app_theme = ThemeFromForegroundColor(
                sender.GetColorValue(winrt::Windows::UI::ViewManagement::UIColorType::Foreground));
            {
              std::lock_guard lock(state->mutex);
              state->queued = fresh;
            }
            PostMessageW(target, signal_message, 0, 0);
          });
      subscribed = true;
    } catch (const winrt::hresult_error&) {
      // The window still gets WM_THEMECHANGED through the fan-out; the
      // UISettings subscription only adds per-app theme changes. Absence is
      // a degradation, not a failure.
    }
    WaitForSingleObject(stop_event, INFINITE);
    if (subscribed) {
      try {
        settings.ColorValuesChanged(token);
      } catch (const winrt::hresult_error&) {
      }
    }
    // Release the COM proxy while the apartment still exists; letting the
    // lambda destroy it after uninit_apartment crashes.
    settings = nullptr;
    winrt::uninit_apartment();
  });
  impl_->monitoring = true;
  if (diagnostic != nullptr) {
    diagnostic->clear();
  }
  return core::Ok();
}

void ThemeAdapter::StopMonitoring() { impl_->Stop(); }

bool ThemeAdapter::Reconcile() {
  // Registry-only, WinRT-free: cheap enough for the UI thread's drain.
  // The WinRT path queues richer snapshots from its callback.
  const ThemeSnapshot current = ReadInitialSnapshot();
  std::lock_guard lock(impl_->state->mutex);
  if (current == impl_->state->snapshot) {
    return impl_->state->queued.has_value() && *impl_->state->queued != impl_->state->snapshot;
  }
  impl_->state->queued = current;
  return true;
}

void ThemeAdapter::QueueSnapshot(const ThemeSnapshot& snapshot) {
  std::lock_guard lock(impl_->state->mutex);
  impl_->state->queued = snapshot;
}

bool ThemeAdapter::ApplyQueuedSnapshot() {
  std::lock_guard lock(impl_->state->mutex);
  if (!impl_->state->queued.has_value() || *impl_->state->queued == impl_->state->snapshot) {
    impl_->state->queued.reset();
    return false;
  }
  impl_->state->snapshot = *impl_->state->queued;
  impl_->state->queued.reset();
  return true;
}

}  // namespace ui::theme

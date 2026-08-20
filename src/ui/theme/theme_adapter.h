// OS theme observation (#54): dark/light selection, debounced application,
// and a WinRT UISettings subscription that is DEFERRED — construction never
// touches WinRT, never spawns a thread, and stays headless-safe. The app
// calls StartMonitoring only after the cold-start budget is paid.
//
// Debounce shape: the WinRT callback only posts a signal; the UI thread's
// coalesced drain calls Reconcile(), which reads the OS once and queues a
// snapshot; ApplyQueuedSnapshot() swaps it in at most once per burst. No
// work happens on the paint path and nothing ticks at idle.
//
// This header stays free of windows.h and winrt.
#pragma once

#include <memory>
#include <string>

#include "core/status.h"

namespace ui::theme {

enum class OsTheme { Dark, Light };
enum class ThemePreference { Dark, Light, System };

struct ThemeSnapshot {
  bool high_contrast = false;
  OsTheme app_theme = OsTheme::Light;
  bool operator==(const ThemeSnapshot&) const = default;
};

// Reads the OS theme without WinRT: the Personalize registry value plus
// SPI_GETHIGHCONTRAST. Cheap, deterministic, headless-safe.
ThemeSnapshot ReadInitialSnapshot() noexcept;

class ThemeAdapter final {
 public:
  ThemeAdapter();
  ~ThemeAdapter();

  ThemeAdapter(const ThemeAdapter&) = delete;
  ThemeAdapter& operator=(const ThemeAdapter&) = delete;

  // Deterministic selection: explicit preferences win; System follows the
  // current snapshot. Returns the core.json theme name ("dark"/"light").
  std::wstring Select(ThemePreference preference) const;

  const ThemeSnapshot& snapshot() const;

  // The deferred WinRT subscription. Spawns one thread whose only job is to
  // post signal_message to `window` when the OS theme changes; the thread
  // parks on a stop event otherwise (no wakeup, no timer). Never called at
  // construction. Returns Ok, or InvalidArgument with `diagnostic` filled
  // when UISettings is unavailable — the app keeps the initial snapshot.
  core::Status StartMonitoring(void* window, unsigned int signal_message,
                               std::wstring* diagnostic);
  void StopMonitoring();

  // Reads the OS now; when it differs from the live snapshot, queues it.
  // Returns true when something was queued.
  bool Reconcile();
  // Test/drain seam: the monitoring thread queues snapshots directly.
  void QueueSnapshot(const ThemeSnapshot& snapshot);
  // Swaps the queued snapshot in; true when the theme actually changed,
  // which is the consumer's invalidate signal. At most one swap per burst
  // no matter how many signals arrived.
  bool ApplyQueuedSnapshot();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ui::theme

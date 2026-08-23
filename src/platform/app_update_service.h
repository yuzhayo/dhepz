#pragma once

#include <functional>
#include <memory>
#include <string>

namespace Velopack {
class UpdateManager;
struct UpdateInfo;
}

namespace update {

struct Snapshot {
  std::wstring current_version;
  std::wstring available_version;
  std::wstring status;
  long long progress = 0;
  bool supported = false;
  bool can_check = false;
  bool can_install = false;
  bool busy = false;
  bool available = false;
};

// Thin platform adapter around Velopack. It deliberately exposes synchronous
// operations: Settings owns the worker boundary, so this class never touches UI
// state or creates a resident thread.
class AppUpdateService final {
 public:
  using Progress = std::function<void(int)>;

  AppUpdateService();
  ~AppUpdateService();

  AppUpdateService(const AppUpdateService&) = delete;
  AppUpdateService& operator=(const AppUpdateService&) = delete;

  Snapshot snapshot() const;
  Snapshot Check();
  Snapshot Download(const Progress& progress);
  bool ScheduleRestart(std::wstring* error);

 private:
  std::unique_ptr<Velopack::UpdateManager> manager_;
  std::unique_ptr<Velopack::UpdateInfo> available_update_;
  Snapshot snapshot_;
};

// Must run at the beginning of wWinMain so install/update lifecycle arguments
// are consumed before dhepz creates its tray or windows.
void RunVelopackStartup();

}  // namespace update

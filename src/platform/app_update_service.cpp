#include "platform/app_update_service.h"

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <utility>

#pragma warning(push)
#pragma warning(disable : 4100)
#include <Velopack.hpp>
#pragma warning(pop)

#include "app_version.h"
#include "platform/strings.h"

namespace update {
namespace {

constexpr const char* kGithubRepositoryUrl = "https://github.com/yuzhayo/dhepz";
constexpr const wchar_t* kLocalSourceVariable = L"DHEPZ_UPDATE_SOURCE";

std::wstring Wide(std::string_view text) {
  std::wstring result;
  return str::FromUtf8(text, &result).ok() ? result : L"Unknown update error";
}

std::wstring ErrorText(const std::exception& error) {
  const std::wstring converted = Wide(error.what());
  return converted.empty() ? L"Unknown update error" : converted;
}

std::unique_ptr<Velopack::UpdateManager> CreateManager() {
  wchar_t source[32768]{};
  const DWORD count = GetEnvironmentVariableW(kLocalSourceVariable, source,
                                               static_cast<DWORD>(std::size(source)));
  if (count > 0 && count < std::size(source)) {
    std::string utf8;
    if (str::ToUtf8(std::wstring_view(source, count), &utf8).ok()) {
      return std::make_unique<Velopack::UpdateManager>(utf8);
    }
  }
  return std::make_unique<Velopack::UpdateManager>(
      std::make_unique<Velopack::GithubSource>(kGithubRepositoryUrl, "", false));
}

}  // namespace

AppUpdateService::AppUpdateService() {
  snapshot_.current_version = dhepz::version::kString;
  snapshot_.status = L"Pembaruan aktif setelah aplikasi di-install.";
  try {
    manager_ = CreateManager();
    const std::string installed = manager_->GetCurrentVersion();
    if (!installed.empty()) snapshot_.current_version = Wide(installed);
    snapshot_.supported = !installed.empty() || manager_->IsPortable();
    snapshot_.can_check = snapshot_.supported;
    snapshot_.status = snapshot_.supported ? L"Siap memeriksa pembaruan."
                                           : L"Pembaruan aktif setelah aplikasi di-install.";
  } catch (const std::exception&) {
    manager_.reset();
  }
}

AppUpdateService::~AppUpdateService() = default;

Snapshot AppUpdateService::snapshot() const { return snapshot_; }

Snapshot AppUpdateService::Check() {
  if (manager_ == nullptr || !snapshot_.supported) return snapshot_;
  try {
    const std::optional<Velopack::UpdateInfo> found = manager_->CheckForUpdates();
    if (!found.has_value()) {
      available_update_.reset();
      snapshot_.available_version.clear();
      snapshot_.status = L"Aplikasi sudah versi terbaru.";
      snapshot_.available = false;
      snapshot_.can_check = true;
      snapshot_.can_install = false;
      snapshot_.busy = false;
      return snapshot_;
    }
    available_update_ = std::make_unique<Velopack::UpdateInfo>(*found);
    snapshot_.available_version = Wide(found->TargetFullRelease.Version);
    snapshot_.status = L"Versi " + snapshot_.available_version + L" tersedia.";
    snapshot_.available = true;
    snapshot_.can_check = true;
    snapshot_.can_install = true;
    snapshot_.busy = false;
  } catch (const std::exception& error) {
    snapshot_.status = L"Pemeriksaan gagal: " + ErrorText(error);
    snapshot_.can_check = true;
    snapshot_.can_install = available_update_ != nullptr;
    snapshot_.busy = false;
  }
  return snapshot_;
}

Snapshot AppUpdateService::Download(const Progress& progress) {
  if (manager_ == nullptr || available_update_ == nullptr) return Check();
  try {
    struct ProgressContext {
      const Progress* callback = nullptr;
    } context{&progress};
    manager_->DownloadUpdates(
        *available_update_,
        [](void* user_data, size_t value) {
          const auto* context = static_cast<const ProgressContext*>(user_data);
          if (context != nullptr && context->callback != nullptr && *context->callback) {
            (*context->callback)(static_cast<int>(std::min<size_t>(100, value)));
          }
        },
        &context);
    snapshot_.progress = 100;
    snapshot_.status = L"Pembaruan siap. Memulai ulang aplikasi...";
    snapshot_.busy = false;
    snapshot_.can_check = false;
    snapshot_.can_install = false;
  } catch (const std::exception& error) {
    snapshot_.status = L"Pembaruan gagal: " + ErrorText(error);
    snapshot_.busy = false;
    snapshot_.can_check = true;
    snapshot_.can_install = available_update_ != nullptr;
  }
  return snapshot_;
}

bool AppUpdateService::ScheduleRestart(std::wstring* error) {
  if (manager_ == nullptr || available_update_ == nullptr) {
    if (error != nullptr) *error = L"Tidak ada pembaruan yang siap dipasang.";
    return false;
  }
  try {
    manager_->WaitExitThenApplyUpdates(*available_update_, false, true, {});
    return true;
  } catch (const std::exception& exception) {
    if (error != nullptr) *error = ErrorText(exception);
    return false;
  }
}

void RunVelopackStartup() { Velopack::VelopackApp::Build().Run(); }

}  // namespace update

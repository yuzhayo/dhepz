#include "ui/settings/settings_update_controller.h"

#include <windows.h>

#include <atomic>
#include <utility>

#include "core/status.h"
#include "parent/ui/runtime/parent_ui.h"
#include "platform/worker.h"
#include "ui/app_window/app_window.h"

namespace ui::settings {
namespace {

constexpr std::wstring_view kCheckAction = L"settings.update.check";
constexpr std::wstring_view kInstallAction = L"settings.update.install";

struct UpdateResult {
  update::Snapshot snapshot;
  bool restart = false;
};

}  // namespace

SettingsUpdateController::SettingsUpdateController() = default;
SettingsUpdateController::~SettingsUpdateController() { Detach(); }

core::Status SettingsUpdateController::Attach(shell::AppWindow* window,
                                              containers::ParentUi* parent,
                                              application::UiActionRegistry* actions) {
  if (window == nullptr || !window->alive() || parent == nullptr || actions == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"Settings updater requires live dependencies");
  }
  Detach();
  window_ = window;
  parent_ = parent;
  completion_message_ = RegisterWindowMessageW(L"dhepz.settings.update.complete.v1");
  if (completion_message_ == 0) {
    return DHEPZ_ERR(core::ErrorCode::Internal,
                     L"Settings update completion message registration failed");
  }
  worker_ = std::make_unique<worker::Worker>(window_->hwnd(), completion_message_);
  generation_ = worker_->CreateGeneration();
  window_->set_native_message_handler([this](unsigned int message, long long lparam) {
    if (message != completion_message_ || worker_ == nullptr) return false;
    worker::Worker::Settle(lparam);
    worker_->JoinFinished();
    return true;
  });
  const auto check = [this](const application::UiEvent&, const application::UiState&) {
    return BeginCheck();
  };
  const auto install = [this](const application::UiEvent&, const application::UiState&) {
    return BeginInstall();
  };
  if (!(actions->Has(kCheckAction) ? actions->Replace(kCheckAction, check)
                                  : actions->Register(std::wstring(kCheckAction), check)) ||
      !(actions->Has(kInstallAction) ? actions->Replace(kInstallAction, install)
                                    : actions->Register(std::wstring(kInstallAction), install))) {
    Detach();
    return DHEPZ_ERR(core::ErrorCode::Internal, L"Settings update actions could not be bound");
  }
  return core::Ok();
}

void SettingsUpdateController::Detach() {
  if (worker_ != nullptr) {
    worker_->InvalidateGeneration(generation_);
    worker_->Shutdown();
  }
  if (window_ != nullptr) window_->set_native_message_handler({});
  worker_.reset();
  generation_ = 0;
  completion_message_ = 0;
  parent_ = nullptr;
  window_ = nullptr;
}

application::UiPatch SettingsUpdateController::InitialPatch() const {
  return PatchFor(service_.snapshot());
}

application::UiPatch SettingsUpdateController::PatchFor(
    const update::Snapshot& snapshot) const {
  std::wstring install_label = L"Update";
  return {{{L"settings.update.version", L"Version " + snapshot.current_version},
           {L"settings.update.status", snapshot.status},
           {L"settings.update.install_label", std::move(install_label)},
           {L"settings.update.can_check", snapshot.can_check},
           {L"settings.update.can_install", snapshot.can_install},
           {L"settings.update.available", snapshot.available}},
          {}};
}

application::UiPatch SettingsUpdateController::BeginCheck() {
  if (worker_ == nullptr || !service_.snapshot().can_check) return {};
  update::Snapshot busy = service_.snapshot();
  busy.status = L"Memeriksa pembaruan...";
  busy.busy = true;
  busy.can_check = false;
  busy.can_install = false;
  worker_->Submit(
      [this](const std::atomic<bool>& cancelled) {
        if (cancelled.load()) return std::shared_ptr<void>{};
        auto result = std::make_shared<UpdateResult>();
        result->snapshot = service_.Check();
        return std::static_pointer_cast<void>(result);
      },
      [this](std::shared_ptr<void> cargo) {
        const auto result = std::static_pointer_cast<UpdateResult>(std::move(cargo));
        if (result != nullptr && parent_ != nullptr) parent_->ApplyPatch(PatchFor(result->snapshot));
      },
      generation_);
  return PatchFor(busy);
}

application::UiPatch SettingsUpdateController::BeginInstall() {
  if (worker_ == nullptr || !service_.snapshot().can_install) return {};
  update::Snapshot busy = service_.snapshot();
  busy.status = L"Mengunduh pembaruan...";
  busy.busy = true;
  busy.can_check = false;
  busy.can_install = false;
  worker_->Submit(
      [this](const std::atomic<bool>& cancelled) {
        auto result = std::make_shared<UpdateResult>();
        result->snapshot = service_.Download([&cancelled](int) {
          // Velopack's C callback cannot cancel the transfer, but retaining
          // the flag here keeps the job compatible with the worker contract.
          (void)cancelled.load();
        });
        if (result->snapshot.progress == 100) {
          std::wstring error;
          result->restart = service_.ScheduleRestart(&error);
          if (!result->restart && !error.empty()) result->snapshot.status = L"Pembaruan gagal: " + error;
        }
        return std::static_pointer_cast<void>(result);
      },
      [this](std::shared_ptr<void> cargo) {
        const auto result = std::static_pointer_cast<UpdateResult>(std::move(cargo));
        if (result == nullptr) return;
        if (parent_ != nullptr) parent_->ApplyPatch(PatchFor(result->snapshot));
        if (result->restart) PostQuitMessage(0);
      },
      generation_);
  return PatchFor(busy);
}

}  // namespace ui::settings

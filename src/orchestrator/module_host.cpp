#include "orchestrator/module_host.h"

#include <windows.h>

#include "parent/ui/runtime/parent_ui.h"
#include "parent/logic/module_state_store.h"
#include "platform/folder_picker.h"
#include "platform/paths.h"
#include "platform/process.h"
#include "platform/worker.h"
#include "ui/app_window/app_window.h"

namespace orchestrator {

ModuleHostAdapter::ModuleHostAdapter(shell::AppWindow* window,
                                     ui::containers::ParentUi* parent,
                                     modules::ModuleStateStore* state_store)
    : window_(window), parent_(parent), state_store_(state_store) {}

ModuleHostAdapter::~ModuleHostAdapter() { Shutdown(); }

core::Status ModuleHostAdapter::Start() {
  if (window_ == nullptr || !window_->alive() || parent_ == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument, L"Module host requires a live window");
  }
  completion_message_ = RegisterWindowMessageW(L"dhepz.module.background.complete.v1");
  if (completion_message_ == 0) {
    return DHEPZ_ERR(core::ErrorCode::Internal, L"Module completion message registration failed");
  }
  worker_ = std::make_unique<worker::Worker>(window_->hwnd(), completion_message_);
  generation_ = worker_->CreateGeneration();
  window_->set_native_message_handler([this](unsigned int message, long long lparam) {
    if (message != completion_message_ || worker_ == nullptr) return false;
    worker::Worker::Settle(lparam);
    worker_->JoinFinished();
    return true;
  });
  return core::Ok();
}

void ModuleHostAdapter::Deactivate() {
  if (worker_ == nullptr) return;
  worker_->InvalidateGeneration(generation_);
  worker_->JoinFinished();
  MSG message{};
  const HWND window = window_ != nullptr ? static_cast<HWND>(window_->hwnd()) : nullptr;
  if (window != nullptr) {
    while (PeekMessageW(&message, window, completion_message_, completion_message_, PM_REMOVE)) {
      worker::Worker::Settle(message.lParam);
    }
  }
}

bool ModuleHostAdapter::Idle() {
  if (worker_ == nullptr) return true;
  worker_->JoinFinished();
  return worker_->ThreadCount() == 0;
}

void ModuleHostAdapter::Shutdown() {
  if (worker_ == nullptr) return;
  Deactivate();
  worker_->Shutdown();
  MSG message{};
  const HWND window = window_ != nullptr ? static_cast<HWND>(window_->hwnd()) : nullptr;
  if (window != nullptr) {
    while (PeekMessageW(&message, window, completion_message_, completion_message_, PM_REMOVE)) {
      worker::Worker::Settle(message.lParam);
    }
    window_->set_native_message_handler({});
  }
  worker_.reset();
  generation_ = 0;
}

std::wstring ModuleHostAdapter::DefaultDirectory() const { return paths::ExecutableDir(); }

ui::application::UiPatch ModuleHostAdapter::RestoredState(std::wstring_view prefix) const {
  return state_store_ != nullptr ? state_store_->Restore(prefix)
                                 : ui::application::UiPatch{};
}

std::optional<std::wstring> ModuleHostAdapter::PickFolder(std::wstring_view initial_path) {
  return folder_picker::Pick(window_ != nullptr ? window_->hwnd() : nullptr, initial_path);
}

void ModuleHostAdapter::RunBackground(modules::BackgroundWork work,
                                      modules::BackgroundComplete complete) {
  if (worker_ == nullptr || !work) return;
  worker_->Submit(
      [this, work = std::move(work)](const std::atomic<bool>& cancelled) {
        return std::make_shared<core::Status>(work(*this, cancelled));
      },
      [complete = std::move(complete)](std::shared_ptr<void> cargo) {
        const auto status = std::static_pointer_cast<core::Status>(std::move(cargo));
        if (complete && status != nullptr) complete(*status);
      },
      generation_);
}

void ModuleHostAdapter::Publish(ui::application::UiPatch patch) {
  if (parent_ != nullptr) parent_->ApplyPatch(patch);
}

void ModuleHostAdapter::CloseWindowIfUnpinned() {
  if (window_ != nullptr && window_->alive() && !window_->pinned()) {
    window_->Close();
  }
}

bool ModuleHostAdapter::DirectoryExists(std::wstring_view path) const {
  return paths::DirectoryExists(path);
}

bool ModuleHostAdapter::FileExists(std::wstring_view path) const {
  return paths::FileExists(path);
}

core::Status ModuleHostAdapter::StartProcess(const modules::ProcessRequest& request) const {
  return process::Start(request);
}

core::Status ModuleHostAdapter::RunProcess(const modules::ProcessRequest& request,
                                           std::wstring* standard_output) const {
  return process::Run(request, standard_output);
}

core::Status ModuleHostAdapter::PersistState(const ui::application::UiPatch& patch) const {
  return state_store_ != nullptr
             ? state_store_->Save(patch)
             : DHEPZ_ERR(core::ErrorCode::Internal, L"Module state store is unavailable");
}

}  // namespace orchestrator

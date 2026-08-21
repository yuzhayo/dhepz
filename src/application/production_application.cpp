#include "application/production_application.h"

#include <windows.h>
#include <dwmapi.h>

#include "core/json.h"
#include "modules/gate/app_gate.h"
#include "platform/performance_trace.h"
#include "platform/tray_process.h"
#include "platform/worker.h"
#include "render/gdi_resource_cache.h"
#include "ui/presenter/screen_presenter.h"
#include "ui/shell/app_window.h"

namespace application {
namespace {

core::Status TrayStartStatus(tray::StartResult result) {
  switch (result) {
    case tray::StartResult::Ok:
      return core::Ok();
    case tray::StartResult::WindowClassFailed:
      return DHEPZ_ERR(core::ErrorCode::Internal,
                       L"Infrastructure window class registration failed");
    case tray::StartResult::WindowCreateFailed:
      return DHEPZ_ERR(core::ErrorCode::Internal,
                       L"Infrastructure window creation failed");
    case tray::StartResult::TaskbarMessageFailed:
      return DHEPZ_ERR(core::ErrorCode::Internal,
                       L"TaskbarCreated message registration failed");
  }
  return DHEPZ_ERR(core::ErrorCode::Internal, L"Unknown tray startup result");
}

void ShowActivationFailure(const core::Status& status) {
  const std::wstring message = L"The launcher window could not be shown.\n\n" +
                               status.Message();
  MessageBoxW(nullptr, message.c_str(), L"dhepz", MB_OK | MB_ICONERROR);
}

}  // namespace

ProductionApplication::ProductionApplication()
    : tray_(std::make_unique<tray::TrayProcess>()) {}

ProductionApplication::~ProductionApplication() { Shutdown(); }

core::Status ProductionApplication::Start(void* instance, bool tray_only) {
  if (started_) {
    return DHEPZ_ERR(core::ErrorCode::AlreadyExists,
                     L"Production application was already started");
  }
  if (instance == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"Production application requires a module instance");
  }

  instance_ = instance;
  DHEPZ_RETURN_IF_ERROR(TrayStartStatus(tray_->Start(instance_)));
  started_ = true;
  tray_->set_activate_handler([this] {
    const core::Status shown = ShowMainWindow();
    if (!shown.ok()) ShowActivationFailure(shown);
  });
  (void)tray_->InstallTray();  // Explorer may be absent; the process may still run.

  if (tray_only) return core::Ok();
  const core::Status shown = ShowMainWindow();
  if (!shown.ok()) Shutdown();
  return shown;
}

core::Status ProductionApplication::ComposeWindow() {
  cache_ = std::make_unique<render::GdiResourceCache>();
  window_ = std::make_unique<shell::AppWindow>(cache_.get());
  if (!window_->Create(instance_)) {
    DestroyWindowState();
    return DHEPZ_ERR(core::ErrorCode::Internal,
                     L"Launcher window creation failed");
  }

  worker_completion_message_ =
      RegisterWindowMessageW(L"dhepz.production.worker.completion");
  if (worker_completion_message_ == 0) {
    DestroyWindowState();
    return DHEPZ_ERR(core::ErrorCode::Internal,
                     L"Worker completion message registration failed");
  }
  window_->set_message_handler(
      [message_id = worker_completion_message_](unsigned int message,
                                                unsigned long long,
                                                long long lparam) {
        if (message != message_id) return false;
        worker::Worker::Settle(lparam);
        return true;
      });

  gate_ = std::make_unique<modules::AppGate>();
  const core::Status host_configured = gate_->ConfigureHostOperations(
      window_->hwnd(), worker_completion_message_,
      [](std::wstring_view, const json::Value&) {
        return DHEPZ_ERR(core::ErrorCode::Unsupported,
                         L"Route state bridge is introduced by IC-07");
      });
  if (!host_configured.ok()) {
    DestroyWindowState();
    return host_configured;
  }
  const core::Status started = gate_->Start();
  if (!started.ok()) {
    DestroyWindowState();
    return started;
  }
  if (gate_->start_pending() || gate_->document() == nullptr) {
    DestroyWindowState();
    return DHEPZ_ERR(core::ErrorCode::Internal,
                     L"Embedded UI startup did not produce a document");
  }

  trace::TraceConfigResolved();
  startup_stage_ = StartupStage::ConfigResolved;

  // Binding the initial module may quarantine it. That is degraded mode, not
  // a fatal shell failure; use the gate's resulting live document.
  (void)gate_->Activate(gate_->document()->initial_route());
  presenter_ = std::make_unique<ui::presenter::ScreenPresenter>(window_->backend());
  presenter_->SetDocument(gate_->document());
  presenter_->set_caption_height(40.0f);

  window_->set_content_layout(
      [this](const render::Rect& content) { OnLayout(content); });
  window_->set_content_painter(
      [this](render::GdiBackend&, const render::Rect& content) {
        presenter_->Paint(content);
      });
  window_->set_content_key_handler(
      [this](int key) { return presenter_->HandleKey(key); });
  window_->set_content_click_handler(
      [this](float x, float y) { return presenter_->HandleClick(x, y); });
  window_->set_content_move_handler(
      [this](float x, float y) { return presenter_->HandleMove(x, y); });
  window_->set_content_down_handler(
      [this](float x, float y) { return presenter_->HandleDown(x, y); });
  window_->set_content_hittest_handler(
      [this](float x, float y) { return presenter_->HitTestContent(x, y); });
  window_->set_frame_presented_handler([this] { OnFramePresented(); });
  window_->set_visibility_handler([this](bool visible) {
    if (!visible && gate_) gate_->ReleaseWindowModules();
  });
  return core::Ok();
}

void ProductionApplication::OnLayout(const render::Rect& content) {
  if (startup_stage_ == StartupStage::ConfigResolved) {
    trace::TraceRenderBufferReady();
    startup_stage_ = StartupStage::RenderBufferReady;
  }
  presenter_->Prepare(content);
  if (startup_stage_ == StartupStage::RenderBufferReady) {
    trace::TraceFirstLayoutComplete();
    startup_stage_ = StartupStage::FirstLayoutComplete;
  }
}

void ProductionApplication::OnFramePresented() {
  if (startup_stage_ == StartupStage::FirstLayoutComplete) {
    trace::TraceFirstPresentComplete();
    startup_stage_ = StartupStage::FirstPresentComplete;
  }
}

core::Status ProductionApplication::ShowMainWindow() {
  if (!started_) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"Production application has not started");
  }
  if (!window_) DHEPZ_RETURN_IF_ERROR(ComposeWindow());

  // Rebind the current route after close/minimise invalidated window lifetime.
  (void)gate_->Activate(presenter_->current_route());
  HWND hwnd = static_cast<HWND>(window_->hwnd());
  if (window_->visible()) {
    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
    return core::Ok();
  }

  window_->Show();
  if (!window_->visible()) {
    return DHEPZ_ERR(core::ErrorCode::Internal,
                     L"Launcher window remained hidden after ShowWindow");
  }
  if (startup_stage_ == StartupStage::FirstPresentComplete &&
      SUCCEEDED(DwmFlush())) {
    trace::TraceFirstFrameVisible();
    startup_stage_ = StartupStage::FirstFrameVisible;
  }
  SetForegroundWindow(hwnd);
  return core::Ok();
}

int ProductionApplication::Run() { return started_ ? tray_->Run() : 1; }

void ProductionApplication::DestroyWindowState() {
  if (gate_) gate_->Shutdown();
  if (window_) {
    window_->set_visibility_handler({});
    window_->set_frame_presented_handler({});
    window_->set_message_handler({});
    window_->set_content_layout({});
    window_->set_content_painter({});
    window_->set_content_key_handler({});
    window_->set_content_click_handler({});
    window_->set_content_move_handler({});
    window_->set_content_down_handler({});
    window_->set_content_hittest_handler({});
  }
  presenter_.reset();
  if (window_) window_->Destroy();
  gate_.reset();
  window_.reset();
  cache_.reset();
  worker_completion_message_ = 0;
  startup_stage_ = StartupStage::None;
}

void ProductionApplication::Shutdown() {
  if (!tray_) return;
  tray_->set_activate_handler({});
  DestroyWindowState();
  tray_->Shutdown();
  started_ = false;
  instance_ = nullptr;
}

void* ProductionApplication::tray_window() const {
  return tray_ ? tray_->window() : nullptr;
}

bool ProductionApplication::startup_trace_complete() const {
  return startup_stage_ == StartupStage::FirstFrameVisible;
}

}  // namespace application

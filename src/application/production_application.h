// Production composition root: owns the parent-level object graph and only
// coordinates lifecycle. Feature logic remains behind AppGate/ModuleHost.
#pragma once

#include <cstdint>
#include <memory>

#include "core/status.h"

namespace modules { class AppGate; }
namespace render { class GdiResourceCache; struct Rect; }
namespace json { class Value; }
namespace shell { class AppWindow; }
namespace tray { class TrayProcess; }
namespace ui::presenter { class ScreenPresenter; }

namespace application {

class ProductionApplication final {
 public:
  ProductionApplication();
  ~ProductionApplication();

  ProductionApplication(const ProductionApplication&) = delete;
  ProductionApplication& operator=(const ProductionApplication&) = delete;

  core::Status Start(void* instance, bool tray_only);
  core::Status ShowMainWindow();
  int Run();
  void Shutdown();

  void* tray_window() const;
  shell::AppWindow* window() const { return window_.get(); }
  modules::AppGate* gate() const { return gate_.get(); }
  ui::presenter::ScreenPresenter* presenter() const { return presenter_.get(); }
  bool startup_trace_complete() const;

 private:
  enum class StartupStage {
    None,
    ConfigResolved,
    RenderBufferReady,
    FirstLayoutComplete,
    FirstPresentComplete,
    FirstFrameVisible,
  };

  core::Status ComposeWindow();
  void DestroyWindowState();
  void OnLayout(const render::Rect& content);
  void OnFramePresented();
  bool OnContentClick(float x, float y);
  core::Status DispatchAction(std::wstring_view route,
                              std::wstring_view action,
                              const json::Value& payload,
                              json::Value* state_patch);
  core::Status OnStatePatch(std::wstring_view module_id,
                            const json::Value& patch);
  void RefreshPresenterDocument();

  void* instance_ = nullptr;
  std::unique_ptr<tray::TrayProcess> tray_;
  std::unique_ptr<render::GdiResourceCache> cache_;
  std::unique_ptr<shell::AppWindow> window_;
  std::unique_ptr<modules::AppGate> gate_;
  std::unique_ptr<ui::presenter::ScreenPresenter> presenter_;
  StartupStage startup_stage_ = StartupStage::None;
  unsigned int worker_completion_message_ = 0;
  std::uint64_t presenter_document_generation_ = 0;
  bool started_ = false;
};

}  // namespace application

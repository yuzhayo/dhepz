#include "ui/settings/settings_window.h"

namespace ui::settings {

SettingsWindow::SettingsWindow() {
  // Reuse AppWindow's native close glyph, hover, hit target and exact caption
  // position. Settings intentionally omits pin and gear.
  window_.set_chrome_buttons(false, true);
}

SettingsWindow::~SettingsWindow() { Close(); }

core::Status SettingsWindow::Open(void* instance, void* owner_window,
                                  const config::ResolvedUiDocument* document) {
  if (document == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"Settings requires its resolved core UI document");
  }
  if (window_.alive()) {
    window_.PlaceBeside(owner_window);
    window_.Show();
    return core::Ok();
  }

  // A previously closed native HWND leaves the generic parent attached to the
  // reusable AppWindow object. Detach before recreating it.
  parent_ui_.Detach();
  if (!window_.Create(instance, 400.0f, 360.0f)) {
    return DHEPZ_ERR(core::ErrorCode::Internal, L"Settings window creation failed");
  }
  const core::Status updater = updater_.Attach(&window_, &parent_ui_, &actions_);
  if (!updater.ok()) {
    window_.Destroy();
    return updater;
  }
  const core::Status attached = parent_ui_.Attach(&window_, document, &actions_);
  if (!attached.ok()) {
    updater_.Detach();
    window_.Destroy();
    return attached;
  }
  parent_ui_.ApplyPatch(updater_.InitialPatch());
  window_.set_close_handler([this] { Close(); });
  window_.PlaceBeside(owner_window);
  window_.Show();
  return core::Ok();
}

void SettingsWindow::Close() {
  updater_.Detach();
  parent_ui_.Detach();
  window_.set_close_handler({});
  window_.Destroy();
}

}  // namespace ui::settings

#include "ui/settings/settings_window.h"

#include <utility>

namespace ui::settings {

SettingsWindow::SettingsWindow() {
  // Reuse AppWindow's native close glyph, hover, hit target and exact caption
  // position. Settings intentionally omits pin and gear.
  window_.set_chrome_buttons(false, true);
  actions_.Register(
      L"settings.tabs.multi_row",
      [this](const application::UiEvent& event, const application::UiState&) {
        const auto* value = std::get_if<bool>(&event.payload);
        if (value != nullptr) {
          tabs_multi_row_ = *value;
          if (tab_layout_changed_) tab_layout_changed_(*value);
        }
        return application::UiPatch{};
      });
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
  parent_ui_.ApplyPatch({{{L"settings.tabs.multi_row", tabs_multi_row_}}, {}});
  window_.set_close_handler([this] { Close(); });
  window_.PlaceBeside(owner_window);
  window_.Show();
  return core::Ok();
}

void SettingsWindow::SetTabLayout(bool multi_row, std::function<void(bool)> changed) {
  tab_layout_changed_ = std::move(changed);
  ApplyTabLayout(multi_row);
}

void SettingsWindow::ApplyTabLayout(bool multi_row) {
  tabs_multi_row_ = multi_row;
  if (window_.alive()) {
    parent_ui_.ApplyPatch({{{L"settings.tabs.multi_row", tabs_multi_row_}}, {}});
  }
}

void SettingsWindow::Close() {
  updater_.Detach();
  parent_ui_.Detach();
  window_.set_close_handler({});
  window_.Destroy();
}

}  // namespace ui::settings

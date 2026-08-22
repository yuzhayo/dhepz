#include "parent/ui/runtime/parent_ui.h"

#include "parent/ui/runtime/screen_presenter.h"
#include "ui/app_window/app_window.h"

namespace ui::containers {

ParentUi::ParentUi() = default;
ParentUi::~ParentUi() { Detach(); }

core::Status ParentUi::Attach(shell::AppWindow* window,
                              const config::ResolvedUiDocument* document,
                              application::UiActionRegistry* actions) {
  if (window == nullptr || !window->alive() || document == nullptr || actions == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument, L"ParentUi requires live dependencies");
  }
  Detach();
  window_ = window;
  presenter_ = std::make_unique<presenter::ScreenPresenter>(window_->backend(), &state_, actions);
  presenter_->SetDocument(document);
  window_->set_content_layout([this](const render::Rect& viewport) {
    presenter_->Prepare({viewport.width, viewport.height});
  });
  window_->set_content_painter([this](render::GdiBackend&, const render::Rect& viewport) {
    presenter_->Paint(viewport);
  });
  window_->set_content_key_handler([this](int key) { return presenter_->HandleKey(key); });
  window_->set_content_text_handler(
      [this](wchar_t character) { return presenter_->HandleText(character); });
  window_->set_content_move_handler(
      [this](float x, float y) { return presenter_->HandleMove(x, y); });
  window_->set_content_down_handler(
      [this](float x, float y) { return presenter_->HandleDown(x, y); });
  window_->set_content_click_handler(
      [this](float x, float y) { return presenter_->HandleClick(x, y); });
  window_->set_content_double_click_handler(
      [this](float x, float y) { return presenter_->HandleDoubleClick(x, y); });
  window_->set_content_context_handler([this](float x, float y) {
    return presenter_->HandleContext(x, y, window_ != nullptr ? window_->hwnd() : nullptr);
  });
  window_->set_content_wheel_handler(
      [this](float x, float y, int delta) { return presenter_->HandleWheel(x, y, delta); });
  return core::Ok();
}

bool ParentUi::ApplyPatch(const application::UiPatch& patch) {
  const bool changed = state_.Apply(patch);
  if (changed && window_ != nullptr) window_->RequestRepaint();
  return changed;
}

void ParentUi::Detach() {
  if (window_ != nullptr) {
    window_->set_content_layout({});
    window_->set_content_painter({});
    window_->set_content_key_handler({});
    window_->set_content_text_handler({});
    window_->set_content_move_handler({});
    window_->set_content_down_handler({});
    window_->set_content_click_handler({});
    window_->set_content_double_click_handler({});
    window_->set_content_context_handler({});
    window_->set_content_wheel_handler({});
  }
  presenter_.reset();
  window_ = nullptr;
}

}  // namespace ui::containers

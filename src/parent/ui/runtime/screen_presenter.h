#pragma once

#include <string>

#include "render/render_backend.h"
#include "parent/ui/contracts/ui_action_registry.h"
#include "parent/ui/contracts/ui_state.h"
#include "ui/components/component_registry.h"
#include "parent/ui/config/resolved_ui_document.h"
#include "parent/ui/runtime/focus_coordinator.h"
#include "parent/ui/runtime/layout_engine.h"

namespace ui::presenter {

class ScreenPresenter final {
 public:
  ScreenPresenter(render::RenderBackend* backend, application::UiState* state,
                  application::UiActionRegistry* actions);

  void SetDocument(const config::ResolvedUiDocument* document);
  void Prepare(render::Size size);
  void Paint(const render::Rect& viewport);

  bool HandleMove(float x, float y);
  bool HandleDown(float x, float y);
  bool HandleClick(float x, float y);
  bool HandleDoubleClick(float x, float y);
  bool HandleContext(float x, float y, void* owner_window);
  bool HandleKey(int virtual_key);
  bool HandleText(wchar_t character);
  bool HandleWheel(float x, float y, int delta);

 private:
  const layout::LayoutNode* Hit(const layout::LayoutNode& node, float x, float y) const;
  const layout::LayoutNode* FindWheel(const layout::LayoutNode& node, float x, float y) const;
  const layout::LayoutNode* FindSource(const layout::LayoutNode& node,
                                       const config::ComponentNode* source) const;
  const layout::LayoutNode* FindDialog(const layout::LayoutNode& node) const;
  void PaintNode(const layout::LayoutNode& node);
  void PaintBackdrop();
  void ApplyDocumentPalette();
  bool Activate(const config::ComponentNode* node);
  bool Dispatch(components::ComponentResult result);

  render::RenderBackend* backend_;
  application::UiState* state_;
  application::UiActionRegistry* actions_;
  components::ComponentRegistry registry_;
  layout::LayoutEngine layout_;
  layout::LayoutEngine backdrop_layout_;
  focus::FocusCoordinator focus_;
  const config::ResolvedUiDocument* document_ = nullptr;
  const layout::LayoutNode* tree_ = nullptr;
  const config::ComponentNode* hovered_ = nullptr;
  const config::ComponentNode* pressed_ = nullptr;
  const config::ComponentNode* expanded_ = nullptr;
  std::wstring route_;
  render::Size viewport_size_;
  components::ComponentPalette palette_;
};

}  // namespace ui::presenter

#pragma once

#include <vector>

#include "render/render_backend.h"
#include "parent/ui/contracts/ui_state.h"
#include "ui/components/component_registry.h"
#include "parent/ui/config/resolved_ui_document.h"

namespace ui::layout {

struct LayoutNode {
  const config::ComponentNode* source = nullptr;
  render::Rect bounds;
  std::vector<LayoutNode> children;
  bool clip_children = false;
};

class LayoutEngine final {
 public:
  LayoutEngine(render::RenderBackend* backend, const components::ComponentRegistry* registry,
               const application::UiState* state);

  const LayoutNode& LayoutRoute(const config::ResolvedUiDocument& document,
                                std::wstring_view route, render::Size size);

 private:
  LayoutNode Build(const config::ComponentNode& node, const render::Rect& available);
  LayoutNode BuildContainer(const config::ComponentNode& node, render::Rect bounds);
  LayoutNode BuildGrid(const config::ComponentNode& node, render::Rect bounds,
                       const render::Rect& content, float gap);
  LayoutNode BuildFlow(const config::ComponentNode& node, render::Rect bounds,
                       const render::Rect& content, float gap);
  render::Size Measure(const config::ComponentNode& node, float max_width);

  render::RenderBackend* backend_;
  const components::ComponentRegistry* registry_;
  const application::UiState* state_;
  LayoutNode tree_;
};

}  // namespace ui::layout

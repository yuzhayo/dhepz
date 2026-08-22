#include "ui/components/tabs/tabs_component.h"

#include "ui/components/component_helpers.h"

namespace ui::components {
namespace {
render::Size Measure(const config::ComponentNode& node, render::RenderBackend&,
                     const application::UiState&, float max_width) {
  return {max_width, static_cast<float>(node.GetInt(L"height", 38))};
}

void Paint(const config::ComponentNode&, const render::Rect& bounds,
           const ComponentVisualState&, const ComponentPalette& palette,
           const application::UiState&, render::RenderBackend& backend) {
  backend.FillRoundedRect(bounds, render::CornerRadius::Uniform(6.0f), palette.surface);
}
}  // namespace

ComponentDescriptor CreateTabsComponent() {
  return {L"tabs", true, &Measure, &Paint, &NeverFocusable, nullptr};
}
}  // namespace ui::components

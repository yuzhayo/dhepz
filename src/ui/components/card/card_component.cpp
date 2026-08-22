#include "ui/components/card/card_component.h"

#include "ui/components/component_helpers.h"

namespace ui::components {
namespace {
void Paint(const config::ComponentNode& node, const render::Rect& bounds,
           const ComponentVisualState& visual, const ComponentPalette& palette,
           const application::UiState& state, render::RenderBackend& backend) {
  render::Color fill = visual.hovered && node.GetBool(L"interactive")
                           ? palette.control_hover
                           : palette.surface;
  backend.FillRoundedRect(bounds, render::CornerRadius::Uniform(7.0f), fill);
  const bool selected = BoundBool(node, L"selected_binding", state);
  backend.StrokeRoundedRect(bounds, render::CornerRadius::Uniform(7.0f),
                            selected ? palette.focus : palette.border,
                            selected ? 2.0f : 1.0f);
  PaintFocus(bounds, visual, palette, backend, 7.0f);
}

bool CanFocus(const config::ComponentNode& node) {
  return node.GetBool(L"interactive") && TabFocusable(node);
}

ComponentResult Activate(const config::ComponentNode& node,
                         const application::UiState& state) {
  if (!node.GetBool(L"interactive")) return {};
  const std::wstring binding = node.GetString(L"selected_binding");
  if (!binding.empty()) {
    return BindingResult(node, L"selected_binding", !state.Bool(binding), true);
  }
  return ActionResult(node);
}
}  // namespace

ComponentDescriptor CreateCardComponent() {
  return {L"card", true, &FillAvailable, &Paint, &CanFocus, &Activate};
}
}  // namespace ui::components

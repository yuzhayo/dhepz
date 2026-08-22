#include "ui/components/button/button_component.h"

#include <algorithm>

#include "ui/components/component_helpers.h"

namespace ui::components {
namespace {
render::Size Measure(const config::ComponentNode& node, render::RenderBackend& backend,
                     const application::UiState&, float max_width) {
  const render::Size label = backend.MeasureText(node.GetString(L"label"), {}, max_width);
  const float width = static_cast<float>(node.GetInt(
      L"width", static_cast<long long>(std::max(32.0f, label.width + 18.0f))));
  const float height = static_cast<float>(node.GetInt(L"height", 32));
  return {std::min(max_width, width), height};
}

void Paint(const config::ComponentNode& node, const render::Rect& bounds,
           const ComponentVisualState& visual, const ComponentPalette& palette,
           const application::UiState&, render::RenderBackend& backend) {
  render::Color fill = visual.pressed ? palette.control_pressed
                                      : visual.hovered ? palette.control_hover : palette.control;
  if (node.GetString(L"variant") == L"danger" && (visual.hovered || visual.pressed)) {
    fill = palette.danger;
  }
  const render::CornerRadius radius = CornerRadiusFor(node);
  if (node.GetString(L"variant") != L"subtle" || visual.hovered || visual.pressed) {
    backend.FillRoundedRect(bounds, radius, fill);
  }
  if (node.GetString(L"variant") != L"subtle") {
    backend.StrokeRoundedRect(bounds, radius, palette.border, 1.0f);
  }
  render::TextStyle label_style;
  label_style.family = node.GetString(L"font_family", L"Segoe UI");
  label_style.size_px = static_cast<float>(node.GetInt(L"font_size", 14));
  backend.DrawTextRun(node.GetString(L"label"), bounds, label_style, palette.text,
                      render::TextAlign::Center, render::VerticalAlign::Middle);
  PaintFocus(bounds, visual, palette, backend);
}

ComponentResult Activate(const config::ComponentNode& node,
                         const application::UiState& state) {
  const std::wstring selected_binding = node.GetString(L"selected_binding");
  if (node.GetBool(L"press_selects") && !selected_binding.empty()) {
    return BindingResult(node, L"selected_binding", !state.Bool(selected_binding), true);
  }
  return ActionResult(node);
}
}  // namespace
ComponentDescriptor CreateButtonComponent() {
  return {L"button", false, &Measure, &Paint, &TabFocusable, &Activate};
}
}  // namespace ui::components

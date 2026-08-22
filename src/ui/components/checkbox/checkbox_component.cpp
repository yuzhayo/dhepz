#include "ui/components/checkbox/checkbox_component.h"

#include <algorithm>

#include "ui/components/component_helpers.h"

namespace ui::components {
namespace {
render::Size Measure(const config::ComponentNode& node, render::RenderBackend& backend,
                     const application::UiState&, float max_width) {
  const render::Size label = backend.MeasureText(node.GetString(L"label"), {}, max_width);
  return {std::min(max_width, label.width + 30.0f), std::max(24.0f, label.height)};
}

void Paint(const config::ComponentNode& node, const render::Rect& bounds,
           const ComponentVisualState& visual, const ComponentPalette& palette,
           const application::UiState& state, render::RenderBackend& backend) {
  const render::Rect box{bounds.x, bounds.y + (bounds.height - 18.0f) / 2.0f, 18.0f, 18.0f};
  const bool checked = BoundBool(node, L"checked_binding", state);
  backend.FillRoundedRect(box, render::CornerRadius::Uniform(3.0f),
                          checked ? palette.focus : palette.control);
  backend.StrokeRoundedRect(box, render::CornerRadius::Uniform(3.0f), palette.border, 1.0f);
  if (checked) {
    backend.DrawTextRun(L"✓", box, {}, palette.text, render::TextAlign::Center,
                        render::VerticalAlign::Middle);
  }
  backend.DrawTextRun(node.GetString(L"label"),
                      {bounds.x + 26.0f, bounds.y, std::max(0.0f, bounds.width - 26.0f),
                       bounds.height},
                      {}, visual.enabled ? palette.text : palette.border,
                      render::TextAlign::Left, render::VerticalAlign::Middle);
  PaintFocus(bounds, visual, palette, backend, 3.0f);
}

ComponentResult Activate(const config::ComponentNode& node,
                         const application::UiState& state) {
  return BindingResult(node, L"checked_binding",
                       !BoundBool(node, L"checked_binding", state), true);
}
}  // namespace

ComponentDescriptor CreateCheckboxComponent() {
  return {L"checkbox", false, &Measure, &Paint, &TabFocusable, &Activate};
}
}  // namespace ui::components

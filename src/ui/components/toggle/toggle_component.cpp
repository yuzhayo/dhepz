#include "ui/components/toggle/toggle_component.h"

#include <algorithm>

#include "ui/components/component_helpers.h"

namespace ui::components {
namespace {
render::Size Measure(const config::ComponentNode& node, render::RenderBackend& backend,
                     const application::UiState&, float max_width) {
  const render::Size label = backend.MeasureText(node.GetString(L"label"), {}, max_width);
  return {std::min(max_width, label.width + 54.0f), std::max(26.0f, label.height)};
}

void Paint(const config::ComponentNode& node, const render::Rect& bounds,
           const ComponentVisualState& visual, const ComponentPalette& palette,
           const application::UiState& state, render::RenderBackend& backend) {
  const bool checked = BoundBool(node, L"checked_binding", state);
  const render::Rect track{bounds.x, bounds.y + (bounds.height - 20.0f) / 2.0f, 40.0f, 20.0f};
  backend.FillRoundedRect(track, render::CornerRadius::Uniform(10.0f),
                          checked ? palette.focus : palette.control);
  const render::Rect thumb{track.x + (checked ? 22.0f : 2.0f), track.y + 2.0f, 16.0f, 16.0f};
  backend.FillRoundedRect(thumb, render::CornerRadius::Uniform(8.0f), palette.text);
  backend.DrawTextRun(node.GetString(L"label"),
                      {bounds.x + 48.0f, bounds.y, std::max(0.0f, bounds.width - 48.0f),
                       bounds.height},
                      {}, visual.enabled ? palette.text : palette.border,
                      render::TextAlign::Left, render::VerticalAlign::Middle);
  PaintFocus(bounds, visual, palette, backend, 10.0f);
}

ComponentResult Activate(const config::ComponentNode& node,
                         const application::UiState& state) {
  return BindingResult(node, L"checked_binding",
                       !BoundBool(node, L"checked_binding", state), true);
}
}  // namespace

ComponentDescriptor CreateToggleComponent() {
  return {L"toggle", false, &Measure, &Paint, &TabFocusable, &Activate};
}
}  // namespace ui::components

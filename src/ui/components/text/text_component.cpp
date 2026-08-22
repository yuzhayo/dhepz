#include "ui/components/text/text_component.h"

#include "ui/components/component_helpers.h"

namespace ui::components {
namespace {
render::Size Measure(const config::ComponentNode& node, render::RenderBackend& backend,
                     const application::UiState& state, float max_width) {
  const std::wstring binding = node.GetString(L"text_binding");
  const std::wstring text =
      binding.empty() ? node.GetString(L"text") : state.Text(binding, node.GetString(L"text"));
  return backend.MeasureText(text, TextStyleFor(node), max_width);
}

void Paint(const config::ComponentNode& node, const render::Rect& bounds,
           const ComponentVisualState&, const ComponentPalette& palette,
           const application::UiState& state, render::RenderBackend& backend) {
  const std::wstring binding = node.GetString(L"text_binding");
  const std::wstring text =
      binding.empty() ? node.GetString(L"text") : state.Text(binding, node.GetString(L"text"));
  backend.DrawTextRun(text, bounds, TextStyleFor(node), palette.text, TextAlignFor(node),
                      render::VerticalAlign::Top);
}
}  // namespace
ComponentDescriptor CreateTextComponent() {
  return {L"text", false, &Measure, &Paint, &NeverFocusable, nullptr};
}
}  // namespace ui::components

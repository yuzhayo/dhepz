#include "ui/components/container/container_component.h"

#include <algorithm>

#include "ui/components/component_helpers.h"

namespace ui::components {
namespace {
void Paint(const config::ComponentNode&, const render::Rect&, const ComponentVisualState&,
           const ComponentPalette&, const application::UiState&, render::RenderBackend&) {}

ComponentResult Wheel(const config::ComponentNode& node, const application::UiState& state,
                      int delta) {
  if (node.GetString(L"overflow") != L"scroll") return {};
  const std::wstring binding = node.GetString(L"scroll_binding");
  if (binding.empty()) return {};
  const long long maximum = std::max(0LL, node.GetInt(L"scroll_maximum"));
  const long long current = state.Integer(binding);
  const long long next = std::clamp(current + (delta > 0 ? -32LL : 32LL), 0LL, maximum);
  if (next == current) return {};
  return BindingResult(node, L"scroll_binding", next, false);
}
}  // namespace
ComponentDescriptor CreateContainerComponent() {
  ComponentDescriptor descriptor{L"container", true, &FillAvailable, &Paint, &NeverFocusable,
                                 nullptr};
  descriptor.wheel = &Wheel;
  return descriptor;
}
}  // namespace ui::components

#include "ui/components/scrollbar/scrollbar_component.h"

#include <algorithm>
#include <windows.h>

#include "ui/components/component_helpers.h"

namespace ui::components {
namespace {
render::Size Measure(const config::ComponentNode& node, render::RenderBackend&,
                     const application::UiState&, float max_width) {
  const float thickness = static_cast<float>(node.GetInt(L"thickness", 12));
  if (node.GetString(L"orientation") == L"horizontal") {
    return {std::min(max_width, static_cast<float>(node.GetInt(L"width", 160))), thickness};
  }
  return {thickness, static_cast<float>(node.GetInt(L"height", 160))};
}

long long Value(const config::ComponentNode& node, const application::UiState& state) {
  return std::clamp(BoundInteger(node, L"value_binding", state), node.GetInt(L"minimum"),
                    node.GetInt(L"maximum", 100));
}

void Paint(const config::ComponentNode& node, const render::Rect& bounds,
           const ComponentVisualState& visual, const ComponentPalette& palette,
           const application::UiState& state, render::RenderBackend& backend) {
  backend.FillRoundedRect(bounds, render::CornerRadius::Uniform(6.0f), palette.surface);
  const long long minimum = node.GetInt(L"minimum");
  const long long maximum = std::max(minimum + 1, node.GetInt(L"maximum", 100));
  const float ratio = static_cast<float>(Value(node, state) - minimum) /
                      static_cast<float>(maximum - minimum);
  const bool horizontal = node.GetString(L"orientation") == L"horizontal";
  const float extent = horizontal ? bounds.width : bounds.height;
  const float thumb_length = std::min(
      extent, std::max(static_cast<float>(node.GetInt(L"minimum_thumb_length", 24)),
                       extent * 0.2f));
  const float offset = ratio * std::max(0.0f, extent - thumb_length);
  const render::Rect thumb = horizontal
                                 ? render::Rect{bounds.x + offset, bounds.y, thumb_length,
                                                bounds.height}
                                 : render::Rect{bounds.x, bounds.y + offset, bounds.width,
                                                thumb_length};
  backend.FillRoundedRect(thumb, render::CornerRadius::Uniform(6.0f),
                          visual.hovered ? palette.control_hover : palette.control);
  PaintFocus(bounds, visual, palette, backend, 6.0f);
}

ComponentResult SetValue(const config::ComponentNode& node, long long value) {
  value = std::clamp(value, node.GetInt(L"minimum"), node.GetInt(L"maximum", 100));
  return BindingResult(node, L"value_binding", value, true);
}

ComponentResult Pointer(const config::ComponentNode& node, const application::UiState&,
                        render::Point point, const render::Rect& bounds,
                        render::RenderBackend&) {
  const bool horizontal = node.GetString(L"orientation") == L"horizontal";
  const float extent = horizontal ? bounds.width : bounds.height;
  if (extent <= 0.0f || !bounds.contains(point)) return {};
  const float position = horizontal ? point.x - bounds.x : point.y - bounds.y;
  const long long minimum = node.GetInt(L"minimum");
  const long long maximum = std::max(minimum, node.GetInt(L"maximum", 100));
  const long long value = minimum + static_cast<long long>(
                                        position / extent * static_cast<float>(maximum - minimum));
  return SetValue(node, value);
}

ComponentResult Key(const config::ComponentNode& node, const application::UiState& state,
                    int key) {
  const long long step = node.GetInt(L"line_step", 1);
  const long long page = node.GetInt(L"page_step", 10);
  const long long current = Value(node, state);
  if (key == VK_LEFT || key == VK_UP) return SetValue(node, current - step);
  if (key == VK_RIGHT || key == VK_DOWN) return SetValue(node, current + step);
  if (key == VK_PRIOR) return SetValue(node, current - page);
  if (key == VK_NEXT) return SetValue(node, current + page);
  if (key == VK_HOME) return SetValue(node, node.GetInt(L"minimum"));
  if (key == VK_END) return SetValue(node, node.GetInt(L"maximum", 100));
  return {};
}

ComponentResult Wheel(const config::ComponentNode& node, const application::UiState& state,
                      int delta) {
  const long long direction = delta > 0 ? -1 : 1;
  return SetValue(node, Value(node, state) + direction * node.GetInt(L"line_step", 1));
}
}  // namespace

ComponentDescriptor CreateScrollbarComponent() {
  ComponentDescriptor descriptor{L"scrollbar", false, &Measure, &Paint, &TabFocusable,
                                 nullptr};
  descriptor.key = &Key;
  descriptor.pointer = &Pointer;
  descriptor.wheel = &Wheel;
  return descriptor;
}
}  // namespace ui::components

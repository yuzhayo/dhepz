#include "ui/components/component_helpers.h"

#include <algorithm>
#include <utility>

namespace ui::components {

render::Size FillAvailable(const config::ComponentNode& node, render::RenderBackend&,
                           const application::UiState&, float max_width) {
  return {node.GetInt(L"width", static_cast<long long>(max_width)) > 0
              ? static_cast<float>(node.GetInt(L"width", static_cast<long long>(max_width)))
              : max_width,
          static_cast<float>(node.GetInt(L"height"))};
}

bool NeverFocusable(const config::ComponentNode&) { return false; }

bool TabFocusable(const config::ComponentNode& node) {
  return node.GetBool(L"tab_stop", true) && node.GetBool(L"visible", true) &&
         node.GetBool(L"enabled", true);
}

std::wstring BoundText(const config::ComponentNode& node, std::wstring_view property,
                       const application::UiState& state, std::wstring_view fallback) {
  const std::wstring binding = node.GetString(property);
  return binding.empty() ? std::wstring(fallback) : state.Text(binding, fallback);
}

bool BoundBool(const config::ComponentNode& node, std::wstring_view property,
               const application::UiState& state, bool fallback) {
  const std::wstring binding = node.GetString(property);
  return binding.empty() ? fallback : state.Bool(binding, fallback);
}

long long BoundInteger(const config::ComponentNode& node, std::wstring_view property,
                       const application::UiState& state, long long fallback) {
  const std::wstring binding = node.GetString(property);
  return binding.empty() ? fallback : state.Integer(binding, fallback);
}

const std::vector<std::wstring>* BoundStrings(const config::ComponentNode& node,
                                              std::wstring_view property,
                                              const application::UiState& state) {
  const std::wstring binding = node.GetString(property);
  return binding.empty() ? nullptr : state.Strings(binding);
}

ComponentResult ActionResult(const config::ComponentNode& node, application::UiValue payload) {
  ComponentResult result;
  result.handled = true;
  result.event.action = node.GetString(L"action");
  result.event.source_id = node.id();
  result.event.payload = std::move(payload);
  return result;
}

ComponentResult BindingResult(const config::ComponentNode& node,
                              std::wstring_view binding_property,
                              application::UiValue value, bool emit_action) {
  ComponentResult result = ActionResult(node, value);
  result.handled = !result.event.action.empty();
  const std::wstring binding = node.GetString(binding_property);
  if (!binding.empty()) {
    result.patch.changes.push_back({binding, std::move(value)});
    result.handled = true;
  }
  if (!emit_action) result.event = {};
  return result;
}

render::TextStyle TextStyleFor(const config::ComponentNode& node) {
  render::TextStyle style;
  const std::wstring variant = node.GetString(L"variant");
  if (variant == L"title") {
    style.size_px = 20.0f;
    style.weight = render::FontWeight::Semibold;
  } else if (variant == L"caption") {
    style.size_px = 12.0f;
  } else if (variant == L"monospace") {
    style.family = L"Cascadia Mono";
  }
  return style;
}

render::TextAlign TextAlignFor(const config::ComponentNode& node) {
  const std::wstring align = node.GetString(L"align");
  if (align == L"center") return render::TextAlign::Center;
  if (align == L"end") return render::TextAlign::Right;
  return render::TextAlign::Left;
}

void PaintFocus(const render::Rect& bounds, const ComponentVisualState& visual,
                const ComponentPalette& palette, render::RenderBackend& backend, float radius) {
  if (!visual.focused) return;
  backend.StrokeRoundedRect(
      {bounds.x - 2.0f, bounds.y - 2.0f, bounds.width + 4.0f, bounds.height + 4.0f},
      render::CornerRadius::Uniform(radius + 2.0f), palette.focus, 2.0f);
}

render::Rect DialogPanelBounds(const config::ComponentNode& node,
                               const render::Rect& viewport) {
  const float available_width = std::max(0.0f, viewport.width - 32.0f);
  const float available_height = std::max(0.0f, viewport.height - 32.0f);
  const float width = std::min(available_width,
                               static_cast<float>(node.GetInt(L"width", 480)));
  const float maximum = static_cast<float>(node.GetInt(L"maximum_height", 720));
  const float requested = static_cast<float>(node.GetInt(L"height", 320));
  const float height = std::min(available_height, std::min(maximum, requested));
  return {viewport.x + (viewport.width - width) / 2.0f,
          viewport.y + (viewport.height - height) / 2.0f, width, height};
}

}  // namespace ui::components

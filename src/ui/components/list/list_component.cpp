#include "ui/components/list/list_component.h"

#include <algorithm>
#include <windows.h>

#include "ui/components/component_helpers.h"

namespace ui::components {
namespace {
render::Size Measure(const config::ComponentNode& node, render::RenderBackend&,
                     const application::UiState&, float max_width) {
  return {std::min(max_width, static_cast<float>(node.GetInt(L"width",
                                                             static_cast<long long>(max_width)))),
          static_cast<float>(node.GetInt(L"height", 160))};
}

std::size_t Offset(const config::ComponentNode& node, const application::UiState& state) {
  return static_cast<std::size_t>(std::max(0LL, BoundInteger(node, L"scroll_binding", state)));
}

void Paint(const config::ComponentNode& node, const render::Rect& bounds,
           const ComponentVisualState& visual, const ComponentPalette& palette,
           const application::UiState& state, render::RenderBackend& backend) {
  backend.FillRoundedRect(bounds, render::CornerRadius::Uniform(5.0f), palette.surface);
  backend.StrokeRoundedRect(bounds, render::CornerRadius::Uniform(5.0f),
                            visual.focused ? palette.focus : palette.border, 1.0f);
  backend.PushClip(bounds);
  const std::vector<std::wstring>* items = BoundStrings(node, L"items_binding", state);
  if (items == nullptr || items->empty()) {
    backend.DrawTextRun(node.GetString(L"empty_text", L"Tidak ada data"), bounds, {},
                        palette.border, render::TextAlign::Center,
                        render::VerticalAlign::Middle);
    backend.PopClip();
    return;
  }
  const float row_height = static_cast<float>(node.GetInt(L"row_height", 32));
  const std::size_t start = std::min(Offset(node, state), items->size());
  const std::size_t visible = static_cast<std::size_t>(bounds.height / row_height) + 1;
  const std::wstring selected = BoundText(node, L"selected_id_binding", state);
  for (std::size_t row = 0; row < visible && start + row < items->size(); ++row) {
    const std::wstring& item = (*items)[start + row];
    const render::Rect row_bounds{bounds.x + 2.0f, bounds.y + row * row_height,
                                  bounds.width - 4.0f, row_height};
    if (item == selected) backend.FillRect(row_bounds, palette.control_pressed);
    backend.DrawTextRun(item,
                        {row_bounds.x + 8.0f, row_bounds.y,
                         std::max(0.0f, row_bounds.width - 16.0f), row_bounds.height},
                        {}, palette.text, render::TextAlign::Left,
                        render::VerticalAlign::Middle);
  }
  backend.PopClip();
}

ComponentResult Pointer(const config::ComponentNode& node, const application::UiState& state,
                        render::Point point, const render::Rect& bounds,
                        render::RenderBackend&) {
  if (node.GetString(L"selection") == L"none") return ActionResult(node);
  const std::vector<std::wstring>* items = BoundStrings(node, L"items_binding", state);
  if (items == nullptr || items->empty() || !bounds.contains(point)) return {};
  const float row_height = static_cast<float>(node.GetInt(L"row_height", 32));
  const std::size_t index = Offset(node, state) +
                            static_cast<std::size_t>((point.y - bounds.y) / row_height);
  if (index >= items->size()) return {};
  return BindingResult(node, L"selected_id_binding", (*items)[index], true);
}

ComponentResult Key(const config::ComponentNode& node, const application::UiState& state,
                    int key) {
  if (node.GetString(L"selection") == L"none") return {};
  const std::vector<std::wstring>* items = BoundStrings(node, L"items_binding", state);
  if (items == nullptr || items->empty()) return {};
  const std::wstring selected = BoundText(node, L"selected_id_binding", state);
  const auto found = std::find(items->begin(), items->end(), selected);
  std::size_t index = found == items->end() ? 0 : static_cast<std::size_t>(found - items->begin());
  if (key == VK_UP) {
    if (index > 0) --index;
  } else if (key == VK_DOWN) {
    if (found != items->end() && index + 1 < items->size()) ++index;
  } else {
    return {};
  }
  return BindingResult(node, L"selected_id_binding", (*items)[index], true);
}

ComponentResult Wheel(const config::ComponentNode& node, const application::UiState& state,
                      int delta) {
  const std::wstring binding = node.GetString(L"scroll_binding");
  const std::vector<std::wstring>* items = BoundStrings(node, L"items_binding", state);
  if (binding.empty() || items == nullptr) return {};
  const long long current = state.Integer(binding);
  const long long next = std::clamp(current + (delta > 0 ? -1LL : 1LL), 0LL,
                                    std::max(0LL, static_cast<long long>(items->size()) - 1));
  if (next == current) return {};
  return BindingResult(node, L"scroll_binding", next, false);
}
}  // namespace

ComponentDescriptor CreateListComponent() {
  ComponentDescriptor descriptor{L"list", false, &Measure, &Paint, &TabFocusable, nullptr};
  descriptor.key = &Key;
  descriptor.pointer = &Pointer;
  descriptor.wheel = &Wheel;
  return descriptor;
}
}  // namespace ui::components

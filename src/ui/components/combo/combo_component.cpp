#include "ui/components/combo/combo_component.h"

#include <algorithm>
#include <windows.h>

#include "ui/components/component_helpers.h"

namespace ui::components {
namespace {
constexpr float kRowHeight = 30.0f;

render::Size Measure(const config::ComponentNode& node, render::RenderBackend&,
                     const application::UiState&, float max_width) {
  return {std::min(max_width, static_cast<float>(node.GetInt(L"width", 220))),
          static_cast<float>(node.GetInt(L"height", 34))};
}

render::Rect PopupBounds(const config::ComponentNode& node, const render::Rect& anchor,
                         render::Size viewport, std::size_t count) {
  const std::size_t maximum =
      static_cast<std::size_t>(std::max(1LL, node.GetInt(L"maximum_visible_items", 10)));
  const float requested = kRowHeight * static_cast<float>(std::min(count, maximum));
  const float height = std::min(requested,
                                static_cast<float>(node.GetInt(L"popup_maximum_height", 480)));
  float y = anchor.bottom();
  if (y + height > viewport.height) y = std::max(0.0f, anchor.y - height);
  return {anchor.x, y, anchor.width, height};
}

void Paint(const config::ComponentNode& node, const render::Rect& bounds,
           const ComponentVisualState& visual, const ComponentPalette& palette,
           const application::UiState& state, render::RenderBackend& backend) {
  backend.FillRoundedRect(bounds, render::CornerRadius::Uniform(5.0f), palette.control);
  backend.StrokeRoundedRect(bounds, render::CornerRadius::Uniform(5.0f),
                            visual.focused ? palette.focus : palette.border,
                            visual.focused ? 2.0f : 1.0f);
  const std::wstring selected = BoundText(node, L"selected_value_binding", state);
  backend.DrawTextRun(selected.empty() ? node.GetString(L"placeholder") : selected,
                      {bounds.x + 9.0f, bounds.y, std::max(0.0f, bounds.width - 38.0f),
                       bounds.height},
                      {}, selected.empty() ? palette.border : palette.text,
                      render::TextAlign::Left, render::VerticalAlign::Middle);
  backend.DrawTextRun(L"▾", {bounds.right() - 30.0f, bounds.y, 24.0f, bounds.height}, {},
                      palette.text, render::TextAlign::Center, render::VerticalAlign::Middle);
}

ComponentResult Select(const config::ComponentNode& node, const application::UiState& state,
                       int direction) {
  const std::vector<std::wstring>* items = BoundStrings(node, L"items_binding", state);
  if (items == nullptr || items->empty()) return {};
  const std::wstring selected = BoundText(node, L"selected_value_binding", state);
  auto found = std::find(items->begin(), items->end(), selected);
  std::size_t index = found == items->end() ? 0 : static_cast<std::size_t>(found - items->begin());
  if (direction < 0) {
    index = index == 0 ? items->size() - 1 : index - 1;
  } else if (found != items->end()) {
    index = (index + 1) % items->size();
  }
  return BindingResult(node, L"selected_value_binding", (*items)[index], true);
}

ComponentResult Key(const config::ComponentNode& node, const application::UiState& state,
                    int key) {
  if (key == VK_UP || key == VK_LEFT) return Select(node, state, -1);
  if (key == VK_DOWN || key == VK_RIGHT) return Select(node, state, 1);
  return {};
}

void PaintOverlay(const config::ComponentNode& node, const render::Rect& anchor,
                  render::Size viewport, const ComponentPalette& palette,
                  const application::UiState& state, render::RenderBackend& backend) {
  const std::vector<std::wstring>* items = BoundStrings(node, L"items_binding", state);
  if (items == nullptr || items->empty()) return;
  const render::Rect popup = PopupBounds(node, anchor, viewport, items->size());
  backend.FillRoundedRect(popup, render::CornerRadius::Uniform(5.0f), palette.surface);
  backend.StrokeRoundedRect(popup, render::CornerRadius::Uniform(5.0f), palette.border, 1.0f);
  const std::wstring selected = BoundText(node, L"selected_value_binding", state);
  const std::size_t visible = static_cast<std::size_t>(popup.height / kRowHeight);
  for (std::size_t index = 0; index < std::min(items->size(), visible); ++index) {
    const render::Rect row{popup.x + 2.0f, popup.y + 2.0f + kRowHeight * index,
                           popup.width - 4.0f, kRowHeight};
    if ((*items)[index] == selected) backend.FillRect(row, palette.control_pressed);
    backend.DrawTextRun((*items)[index], {row.x + 7.0f, row.y, row.width - 14.0f, row.height},
                        {}, palette.text, render::TextAlign::Left,
                        render::VerticalAlign::Middle);
  }
}

ComponentResult OverlayPointer(const config::ComponentNode& node,
                               const application::UiState& state, render::Point point,
                               const render::Rect& anchor, render::Size viewport) {
  const std::vector<std::wstring>* items = BoundStrings(node, L"items_binding", state);
  if (items == nullptr || items->empty()) return {};
  const render::Rect popup = PopupBounds(node, anchor, viewport, items->size());
  if (!popup.contains(point)) return {};
  const std::size_t index = static_cast<std::size_t>((point.y - popup.y) / kRowHeight);
  if (index >= items->size()) return {};
  return BindingResult(node, L"selected_value_binding", (*items)[index], true);
}
}  // namespace

ComponentDescriptor CreateComboComponent() {
  ComponentDescriptor descriptor{L"combo", false, &Measure, &Paint, &TabFocusable, nullptr};
  descriptor.key = &Key;
  descriptor.paint_overlay = &PaintOverlay;
  descriptor.overlay_pointer = &OverlayPointer;
  return descriptor;
}
}  // namespace ui::components

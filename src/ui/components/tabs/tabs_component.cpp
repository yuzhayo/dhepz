#include "ui/components/tabs/tabs_component.h"

#include <windows.h>

#include <algorithm>
#include <cmath>

#include "ui/components/component_helpers.h"

namespace ui::components {
namespace {
constexpr float kEdgePadding = 4.0f;
constexpr float kTabStart = 16.0f;
constexpr float kLockWidth = 32.0f;
constexpr long long kHoldMilliseconds = 500;
constexpr long long kNoIndex = -1;
constexpr long long kLockIndex = -2;

struct Geometry {
  float gap = 3.0f;
  float tab_width = 112.0f;
  float tab_height = 32.0f;
  std::size_t columns = 1;
  std::size_t rows = 1;
  render::Rect lock;
};

render::TextStyle TabTextStyle() {
  render::TextStyle style;
  style.size_px = 13.0f;
  return style;
}

render::TextStyle LockTextStyle() {
  render::TextStyle style;
  style.family = L"Segoe MDL2 Assets";
  style.size_px = 14.0f;
  return style;
}

const std::vector<std::wstring>* Strings(const config::ComponentNode& node,
                                         std::wstring_view property,
                                         const application::UiState& state) {
  return state.Strings(node.GetString(property));
}

Geometry Calculate(const config::ComponentNode& node, const application::UiState& state,
                   const render::Rect& bounds) {
  Geometry geometry;
  geometry.gap = static_cast<float>(node.GetInt(L"gap", 3));
  geometry.tab_height = static_cast<float>(node.GetInt(L"tab_height", 32));
  const auto* routes = Strings(node, L"routes_binding", state);
  const std::size_t count = routes != nullptr ? routes->size() : 0;
  const float available = std::max(
      0.0f, bounds.width - kTabStart - kEdgePadding - kLockWidth - geometry.gap);
  const bool multi_row = BoundBool(node, L"multi_row_binding", state, true);
  const float preferred = static_cast<float>(node.GetInt(L"tab_width", 112));
  if (count > 0) {
    if (multi_row) {
      const auto fitting = static_cast<std::size_t>(
          std::floor((available + geometry.gap) / (preferred + geometry.gap)));
      geometry.columns = std::max<std::size_t>(1, std::min(count, fitting));
      geometry.rows = (count + geometry.columns - 1) / geometry.columns;
    } else {
      geometry.columns = count;
      geometry.rows = 1;
    }
    const float fitted =
        (available - geometry.gap * static_cast<float>(geometry.columns - 1)) /
        static_cast<float>(geometry.columns);
    geometry.tab_width = std::min(preferred, std::max(1.0f, fitted));
  }
  geometry.lock = {bounds.right() - kEdgePadding - kLockWidth, bounds.y + kEdgePadding,
                   kLockWidth, geometry.tab_height};
  return geometry;
}

render::Rect TabBounds(const Geometry& geometry, const render::Rect& bounds,
                       std::size_t index) {
  const std::size_t column = index % geometry.columns;
  const std::size_t row = index / geometry.columns;
  return {bounds.x + kTabStart + static_cast<float>(column) *
                                    (geometry.tab_width + geometry.gap),
          bounds.y + kEdgePadding + static_cast<float>(row) *
                                    (geometry.tab_height + geometry.gap),
          geometry.tab_width, geometry.tab_height};
}

long long HitIndex(const config::ComponentNode& node, const application::UiState& state,
                   render::Point point, const render::Rect& bounds) {
  const Geometry geometry = Calculate(node, state, bounds);
  if (geometry.lock.contains(point)) return kLockIndex;
  const auto* routes = Strings(node, L"routes_binding", state);
  if (routes == nullptr) return kNoIndex;
  for (std::size_t index = 0; index < routes->size(); ++index) {
    if (TabBounds(geometry, bounds, index).contains(point)) {
      return static_cast<long long>(index);
    }
  }
  return kNoIndex;
}

render::Size Measure(const config::ComponentNode& node, render::RenderBackend& backend,
                     const application::UiState& state, float max_width) {
  const Geometry geometry = Calculate(node, state, {0.0f, 0.0f, max_width, 0.0f});
  const auto* routes = Strings(node, L"routes_binding", state);
  const auto* labels = Strings(node, L"labels_binding", state);
  if (routes != nullptr) {
    for (std::size_t index = 0; index < routes->size(); ++index) {
      const std::wstring& label = labels != nullptr && index < labels->size()
                                      ? (*labels)[index]
                                      : (*routes)[index];
      (void)backend.MeasureText(label, TabTextStyle(), geometry.tab_width);
    }
  }
  (void)backend.MeasureText(L"\uE72E\uE785", LockTextStyle(), kLockWidth);
  return {max_width, kEdgePadding * 2.0f + geometry.rows * geometry.tab_height +
                         static_cast<float>(geometry.rows - 1) * geometry.gap};
}

void Paint(const config::ComponentNode& node, const render::Rect& bounds,
           const ComponentVisualState&, const ComponentPalette& palette,
           const application::UiState& state, render::RenderBackend& backend) {
  const auto* routes = Strings(node, L"routes_binding", state);
  const auto* labels = Strings(node, L"labels_binding", state);
  if (routes == nullptr || routes->empty()) return;
  const Geometry geometry = Calculate(node, state, bounds);
  const std::wstring selected = state.Text(node.GetString(L"selected_binding"));
  const long long hovered = state.Integer(L"parent.tabs.hovered_index", kNoIndex);
  const long long pressed = state.Integer(L"parent.tabs.pressed_index", kNoIndex);
  const long long drag_to = state.Integer(L"parent.tabs.drag_to", kNoIndex);
  const render::TextStyle text_style = TabTextStyle();
  for (std::size_t index = 0; index < routes->size(); ++index) {
    const render::Rect tab = TabBounds(geometry, bounds, index);
    const bool active = (*routes)[index] == selected;
    render::Color fill = active ? palette.control_pressed : palette.surface;
    if (static_cast<long long>(index) == pressed || static_cast<long long>(index) == drag_to) {
      fill = palette.control_pressed;
    } else if (static_cast<long long>(index) == hovered) {
      fill = palette.control_hover;
    }
    backend.FillRoundedRect(tab, {8.0f, 8.0f, active ? 2.0f : 8.0f,
                                  active ? 2.0f : 8.0f}, fill);
    if (active) {
      backend.FillRoundedRect({tab.x + 10.0f, tab.bottom() - 3.0f,
                               std::max(0.0f, tab.width - 20.0f), 2.0f},
                              render::CornerRadius::Uniform(1.0f), palette.focus);
    }
    const std::wstring& label = labels != nullptr && index < labels->size()
                                    ? (*labels)[index]
                                    : (*routes)[index];
    backend.DrawTextRun(label, {tab.x + 8.0f, tab.y, std::max(0.0f, tab.width - 16.0f),
                                tab.height},
                        text_style, palette.text, render::TextAlign::Center,
                        render::VerticalAlign::Middle);
  }

  const bool locked = BoundBool(node, L"locked_binding", state);
  const bool lock_hovered = hovered == kLockIndex;
  if (lock_hovered || pressed == kLockIndex || locked) {
    backend.FillRoundedRect(geometry.lock, render::CornerRadius::Uniform(8.0f),
                            pressed == kLockIndex ? palette.control_pressed
                            : lock_hovered         ? palette.control_hover
                                                   : palette.surface);
  }
  const render::TextStyle icon = LockTextStyle();
  backend.DrawTextRun(locked ? L"\uE72E" : L"\uE785", geometry.lock, icon, palette.text,
                      render::TextAlign::Center, render::VerticalAlign::Middle);
}

ComponentResult Down(const config::ComponentNode& node, const application::UiState& state,
                     render::Point point, const render::Rect& bounds) {
  const long long hit = HitIndex(node, state, point, bounds);
  if (hit == kNoIndex) return {};
  ComponentResult result;
  result.handled = true;
  result.patch.changes = {{L"parent.tabs.pressed_index", hit},
                          {L"parent.tabs.drag_from", hit},
                          {L"parent.tabs.drag_to", hit},
                          {L"parent.tabs.drag_active", false},
                          {L"parent.tabs.hold_started", static_cast<long long>(GetTickCount64())}};
  return result;
}

ComponentResult Move(const config::ComponentNode& node, const application::UiState& state,
                     render::Point point, const render::Rect& bounds) {
  ComponentResult result;
  const long long hit = point.x >= 0.0f && point.y >= 0.0f
                            ? HitIndex(node, state, point, bounds)
                            : kNoIndex;
  result.patch.changes.push_back({L"parent.tabs.hovered_index", hit});
  const long long from = state.Integer(L"parent.tabs.drag_from", kNoIndex);
  if (from < 0 || hit < 0 || BoundBool(node, L"locked_binding", state)) {
    result.handled = state.Integer(L"parent.tabs.hovered_index", kNoIndex) != hit;
    return result;
  }
  const long long started = state.Integer(L"parent.tabs.hold_started", 0);
  const bool active = state.Bool(L"parent.tabs.drag_active") ||
                      static_cast<long long>(GetTickCount64()) - started >= kHoldMilliseconds;
  if (!active) return result;
  result.handled = true;
  result.patch.changes.push_back({L"parent.tabs.drag_active", true});
  if (hit != from) {
    result.patch.changes.push_back({L"parent.tabs.drag_from", hit});
    result.patch.changes.push_back({L"parent.tabs.drag_to", hit});
    result.event.action = node.GetString(L"reorder_action");
    result.event.source_id = node.id();
    result.event.payload = std::to_wstring(from) + L":" + std::to_wstring(hit);
  }
  return result;
}

ComponentResult Up(const config::ComponentNode& node, const application::UiState& state,
                   render::Point point, const render::Rect& bounds) {
  const long long released = point.x >= 0.0f && point.y >= 0.0f
                                 ? HitIndex(node, state, point, bounds)
                                 : kNoIndex;
  const long long pressed = state.Integer(L"parent.tabs.pressed_index", kNoIndex);
  const bool dragged = state.Bool(L"parent.tabs.drag_active");
  ComponentResult result;
  result.handled = pressed != kNoIndex;
  result.patch.changes = {{L"parent.tabs.pressed_index", kNoIndex},
                          {L"parent.tabs.drag_from", kNoIndex},
                          {L"parent.tabs.drag_to", kNoIndex},
                          {L"parent.tabs.drag_active", false},
                          {L"parent.tabs.hold_started", 0LL}};
  if (released != pressed || dragged) return result;
  if (released == kLockIndex) {
    result.event.action = node.GetString(L"lock_action");
    result.event.source_id = node.id();
    result.event.payload = !BoundBool(node, L"locked_binding", state);
    return result;
  }
  const auto* routes = Strings(node, L"routes_binding", state);
  if (released >= 0 && routes != nullptr && static_cast<std::size_t>(released) < routes->size()) {
    result.event.action = node.GetString(L"select_action");
    result.event.source_id = node.id();
    result.event.payload = (*routes)[static_cast<std::size_t>(released)];
  }
  return result;
}
}  // namespace

ComponentDescriptor CreateTabsComponent() {
  ComponentDescriptor descriptor{L"tabs", true, &Measure, &Paint, &NeverFocusable, nullptr};
  descriptor.pointer_down = &Down;
  descriptor.pointer_move = &Move;
  descriptor.pointer_up = &Up;
  return descriptor;
}
}  // namespace ui::components

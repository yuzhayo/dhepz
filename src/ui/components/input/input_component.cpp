#include "ui/components/input/input_component.h"

#include <algorithm>
#include <utility>
#include <windows.h>

#include "ui/components/component_helpers.h"

namespace ui::components {
namespace {
render::Size Measure(const config::ComponentNode& node, render::RenderBackend&,
                     const application::UiState&, float max_width) {
  const float width = std::min(max_width, static_cast<float>(node.GetInt(L"width", 240)));
  const float default_height = node.GetString(L"mode") == L"multiline" ? 96.0f : 34.0f;
  return {width, static_cast<float>(node.GetInt(L"height",
                                               static_cast<long long>(default_height)))};
}

std::wstring Value(const config::ComponentNode& node, const application::UiState& state) {
  return BoundText(node, L"value_binding", state);
}

ComponentResult Changed(const config::ComponentNode& node, std::wstring value) {
  return BindingResult(node, L"value_binding", std::move(value), true);
}

void Paint(const config::ComponentNode& node, const render::Rect& bounds,
           const ComponentVisualState& visual, const ComponentPalette& palette,
           const application::UiState& state, render::RenderBackend& backend) {
  backend.FillRoundedRect(bounds, render::CornerRadius::Uniform(5.0f), palette.control);
  backend.StrokeRoundedRect(bounds, render::CornerRadius::Uniform(5.0f),
                            visual.focused ? palette.focus : palette.border,
                            visual.focused ? 2.0f : 1.0f);
  std::wstring value = Value(node, state);
  const bool placeholder = value.empty();
  if (placeholder) value = node.GetString(L"placeholder");
  if (!placeholder && node.GetBool(L"password")) value.assign(value.size(), L'•');
  if (visual.focused && !node.GetBool(L"read_only")) value.push_back(L'|');
  const std::wstring align = node.GetString(L"horizontal_align");
  const render::TextAlign text_align = align == L"center"
                                           ? render::TextAlign::Center
                                           : align == L"end" ? render::TextAlign::Right
                                                             : render::TextAlign::Left;
  backend.DrawTextRun(value,
                      {bounds.x + 9.0f, bounds.y + 2.0f,
                       std::max(0.0f, bounds.width - 18.0f), bounds.height - 4.0f},
                      {}, placeholder ? palette.border : palette.text, text_align,
                      node.GetString(L"mode") == L"multiline"
                          ? render::VerticalAlign::Top
                          : render::VerticalAlign::Middle);
}

ComponentResult Key(const config::ComponentNode& node, const application::UiState& state,
                    int key) {
  if (node.GetBool(L"read_only")) return {};
  std::wstring value = Value(node, state);
  if (key == VK_BACK) {
    if (value.empty()) return {};
    value.pop_back();
    return Changed(node, std::move(value));
  }
  if (key == VK_DELETE) {
    if (value.empty()) return {};
    value.clear();
    return Changed(node, std::move(value));
  }
  if (key == VK_RETURN && node.GetString(L"mode") == L"multiline" &&
      value.size() < static_cast<std::size_t>(node.GetInt(L"maximum_length", 4096))) {
    value.push_back(L'\n');
    return Changed(node, std::move(value));
  }
  return {};
}

ComponentResult TextInput(const config::ComponentNode& node,
                          const application::UiState& state, wchar_t character) {
  if (node.GetBool(L"read_only") || character < L' ' || character == 0x7F) return {};
  std::wstring value = Value(node, state);
  if (value.size() >= static_cast<std::size_t>(node.GetInt(L"maximum_length", 4096))) return {};
  value.push_back(character);
  return Changed(node, std::move(value));
}
}  // namespace

ComponentDescriptor CreateInputComponent() {
  ComponentDescriptor descriptor{L"input", false, &Measure, &Paint, &TabFocusable, nullptr};
  descriptor.key = &Key;
  descriptor.text_input = &TextInput;
  return descriptor;
}
}  // namespace ui::components

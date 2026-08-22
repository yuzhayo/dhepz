#include "ui/components/dialog/dialog_component.h"

#include <windows.h>

#include "ui/components/component_helpers.h"

namespace ui::components {
namespace {
void Paint(const config::ComponentNode& node, const render::Rect& bounds,
           const ComponentVisualState&, const ComponentPalette& palette,
           const application::UiState&, render::RenderBackend& backend) {
  backend.FillRect(bounds, {0, 0, 0, 150});
  const render::Rect panel = DialogPanelBounds(node, bounds);
  backend.FillRoundedRect(panel, render::CornerRadius::Uniform(8.0f), palette.surface);
  backend.StrokeRoundedRect(panel, render::CornerRadius::Uniform(8.0f), palette.border, 1.0f);
  const std::wstring title = node.GetString(L"title");
  if (!title.empty()) {
    render::TextStyle title_style;
    title_style.size_px = 16.0f;
    title_style.weight = render::FontWeight::Semibold;
    backend.DrawTextRun(title,
                        {panel.x + 16.0f, panel.y + 12.0f,
                         std::max(0.0f, panel.width - 32.0f), 28.0f},
                        title_style, palette.text,
                        render::TextAlign::Left, render::VerticalAlign::Middle);
  }
}

ComponentResult Key(const config::ComponentNode& node, const application::UiState&,
                     int key) {
  if (key != VK_ESCAPE || !node.GetBool(L"dismiss_escape", true)) return {};
  return BindingResult(node, L"open_binding", false, true);
}

ComponentResult Pointer(const config::ComponentNode& node, const application::UiState&,
                        render::Point point, const render::Rect& bounds) {
  if (!node.GetBool(L"dismiss_outside_click")) return {};
  if (!DialogPanelBounds(node, bounds).contains(point)) {
    return BindingResult(node, L"open_binding", false, true);
  }
  return {};
}
}  // namespace

ComponentDescriptor CreateDialogComponent() {
  ComponentDescriptor descriptor{L"dialog", true, &FillAvailable, &Paint, &NeverFocusable,
                                 nullptr};
  descriptor.key = &Key;
  descriptor.pointer = &Pointer;
  return descriptor;
}
}  // namespace ui::components

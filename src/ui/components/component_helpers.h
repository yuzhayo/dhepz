#pragma once

#include "ui/components/component.h"

namespace ui::components {

render::Size FillAvailable(const config::ComponentNode& node, render::RenderBackend& backend,
                           const application::UiState& state, float max_width);
bool NeverFocusable(const config::ComponentNode& node);
bool TabFocusable(const config::ComponentNode& node);
std::wstring BoundText(const config::ComponentNode& node, std::wstring_view property,
                       const application::UiState& state,
                       std::wstring_view fallback = {});
bool BoundBool(const config::ComponentNode& node, std::wstring_view property,
               const application::UiState& state, bool fallback = false);
long long BoundInteger(const config::ComponentNode& node, std::wstring_view property,
                       const application::UiState& state, long long fallback = 0);
const std::vector<std::wstring>* BoundStrings(const config::ComponentNode& node,
                                              std::wstring_view property,
                                              const application::UiState& state);
ComponentResult BindingResult(const config::ComponentNode& node,
                              std::wstring_view binding_property,
                              application::UiValue value, bool emit_action = true);
ComponentResult ActionResult(const config::ComponentNode& node,
                             application::UiValue payload = {});
render::TextStyle TextStyleFor(const config::ComponentNode& node);
render::TextAlign TextAlignFor(const config::ComponentNode& node);
void PaintFocus(const render::Rect& bounds, const ComponentVisualState& visual,
                const ComponentPalette& palette, render::RenderBackend& backend,
                float radius = 5.0f);
render::Rect DialogPanelBounds(const config::ComponentNode& node,
                               const render::Rect& viewport);

}  // namespace ui::components

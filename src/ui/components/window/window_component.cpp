#include "ui/components/window/window_component.h"

#include "ui/components/component_helpers.h"

namespace ui::components {
namespace {
void Paint(const config::ComponentNode&, const render::Rect&, const ComponentVisualState&,
           const ComponentPalette&, const application::UiState&, render::RenderBackend&) {}
}  // namespace

ComponentDescriptor CreateWindowComponent() {
  return {L"window", true, &FillAvailable, &Paint, &NeverFocusable, nullptr};
}
}  // namespace ui::components

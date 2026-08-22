#include "ui/components/screen/screen_component.h"

#include "ui/components/component_helpers.h"

namespace ui::components {
namespace {
void Paint(const config::ComponentNode&, const render::Rect&, const ComponentVisualState&,
           const ComponentPalette&, const application::UiState&, render::RenderBackend&) {}
}  // namespace
ComponentDescriptor CreateScreenComponent() {
  return {L"screen", true, &FillAvailable, &Paint, &NeverFocusable, nullptr};
}
}  // namespace ui::components

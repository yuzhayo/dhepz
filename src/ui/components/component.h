#pragma once

#include <string_view>

#include "render/render_backend.h"
#include "parent/ui/config/resolved_ui_document.h"
#include "parent/ui/contracts/ui_contract.h"
#include "parent/ui/contracts/ui_state.h"

namespace ui::components {

struct ComponentPalette {
  render::Color surface{30, 30, 30, 255};
  render::Color control{44, 44, 44, 255};
  render::Color control_hover{58, 58, 58, 255};
  render::Color control_pressed{72, 72, 72, 255};
  render::Color border{90, 90, 90, 255};
  render::Color text{235, 235, 235, 255};
  render::Color focus{96, 165, 250, 255};
  render::Color danger{232, 17, 35, 255};
};

struct ComponentVisualState {
  bool hovered = false;
  bool pressed = false;
  bool focused = false;
  bool enabled = true;
};

struct ComponentResult {
  bool handled = false;
  application::UiPatch patch;
  application::UiEvent event;
};

using MeasureFn = render::Size (*)(const config::ComponentNode&, render::RenderBackend&,
                                   const application::UiState&, float);
using PaintFn = void (*)(const config::ComponentNode&, const render::Rect&,
                         const ComponentVisualState&, const ComponentPalette&,
                         const application::UiState&, render::RenderBackend&);
using FocusFn = bool (*)(const config::ComponentNode&);
using ActivateFn = ComponentResult (*)(const config::ComponentNode&,
                                       const application::UiState&);
using KeyFn = ComponentResult (*)(const config::ComponentNode&, const application::UiState&, int);
using TextInputFn = ComponentResult (*)(const config::ComponentNode&,
                                        const application::UiState&, wchar_t);
using PointerFn = ComponentResult (*)(const config::ComponentNode&,
                                      const application::UiState&, render::Point,
                                      const render::Rect&, render::RenderBackend&);
using WheelFn = ComponentResult (*)(const config::ComponentNode&,
                                    const application::UiState&, int);
using OverlayPaintFn = void (*)(const config::ComponentNode&, const render::Rect&,
                                render::Size, const ComponentPalette&,
                                const application::UiState&, render::RenderBackend&);
using OverlayPointerFn = ComponentResult (*)(const config::ComponentNode&,
                                             const application::UiState&, render::Point,
                                             const render::Rect&, render::Size);
using DoubleClickFn = ComponentResult (*)(const config::ComponentNode&,
                                          const application::UiState&);
using ContextMenuFn = ComponentResult (*)(const config::ComponentNode&,
                                          const application::UiState&, void* owner_window);
using HasOverlayFn = bool (*)(const config::ComponentNode&,
                              const application::UiState&);

struct ComponentDescriptor {
  std::wstring_view type;
  bool container = false;
  MeasureFn measure = nullptr;
  PaintFn paint = nullptr;
  FocusFn can_focus = nullptr;
  ActivateFn activate = nullptr;
  KeyFn key = nullptr;
  TextInputFn text_input = nullptr;
  PointerFn pointer = nullptr;
  WheelFn wheel = nullptr;
  OverlayPaintFn paint_overlay = nullptr;
  OverlayPointerFn overlay_pointer = nullptr;
  DoubleClickFn double_click = nullptr;
  ContextMenuFn context_menu = nullptr;
  HasOverlayFn has_overlay = nullptr;
};

}  // namespace ui::components

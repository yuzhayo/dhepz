// Backdrop layers (#62): the z-order behind a screen's content.
//
//   [backdrop] < [screen fill, only when no backdrop] < [components by z]
//
// A backdrop is a color token, an image, or another route. A screen-route
// backdrop is painted from the layout engine's CACHED tree — laid out once,
// never per front frame — so the front pays draw calls only. There is no
// separate backdrop bitmap: nothing extra is allocated, and the hide-release
// lifecycle of the shell's buffer covers it by construction.
#pragma once

#include <string>

#include "render/render_backend.h"
#include "ui/config/resolved_ui_document.h"
#include "ui/layout/layout_engine.h"

namespace ui::layout {

// What the paint pass must do for a route's background.
struct PaintPlan {
  bool content_fill_transparent = false;  // true when a backdrop is present
  config::Route::BackdropKind kind = config::Route::BackdropKind::None;
  std::wstring value;
};

PaintPlan MakePaintPlan(const config::ResolvedUiDocument& document, std::wstring_view route);

// Paints the backdrop layer into the current frame (caller owns the frame
// scope). `theme` selects the token map for color backdrops.
void PaintBackdrop(render::RenderBackend* backend, LayoutEngine* engine,
                   const config::ResolvedUiDocument& document, std::wstring_view route,
                   render::Size size, std::wstring_view theme, const ListModel* model);

// Draw-only variant for a screen backdrop: paints `tree` (laid out by the
// caller outside the frame scope). No measurement happens here.
void PaintBackdropTree(render::RenderBackend* backend, const LayoutNode& tree);

}  // namespace ui::layout

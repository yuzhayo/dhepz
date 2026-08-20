#include "ui/layout/backdrop.h"

namespace ui::layout {
namespace {

void PaintTree(render::RenderBackend* backend, const LayoutNode& node) {
  if (node.source != nullptr && node.source->type() == L"text") {
    render::TextStyle style;
    backend->DrawTextRun(node.source->GetString(L"text"), node.bounds, style,
                         render::Color{255, 255, 255, 255}, render::TextAlign::Left,
                         render::VerticalAlign::Top);
  }
  for (const LayoutNode& child : node.children) {
    PaintTree(backend, child);
  }
}

}  // namespace

PaintPlan MakePaintPlan(const config::ResolvedUiDocument& document, std::wstring_view route) {
  PaintPlan plan;
  const config::Route* found = document.FindRoute(route);
  if (found == nullptr) return plan;
  plan.kind = found->backdrop_kind;
  plan.value = found->backdrop_value;
  plan.content_fill_transparent = plan.kind != config::Route::BackdropKind::None;
  return plan;
}

void PaintBackdrop(render::RenderBackend* backend, LayoutEngine* engine,
                   const config::ResolvedUiDocument& document, std::wstring_view route,
                   render::Size size, std::wstring_view theme, const ListModel* model) {
  const config::Route* found = document.FindRoute(route);
  if (found == nullptr || found->backdrop_kind == config::Route::BackdropKind::None) {
    return;
  }
  const render::Rect full{0.0f, 0.0f, size.width, size.height};
  switch (found->backdrop_kind) {
    case config::Route::BackdropKind::Color: {
      config::Rgba color{};
      if (document.Token(theme, found->backdrop_value, &color)) {
        backend->FillRect(full, render::Color{color.r, color.g, color.b, color.a});
      }
      return;
    }
    case config::Route::BackdropKind::Image: {
      const render::ImageHandle image = backend->LoadImageFile(found->backdrop_value);
      if (image != render::ImageHandle::Invalid) {
        backend->DrawImage(image, full, 1.0f);
        backend->ReleaseImage(image);
      }
      return;
    }
    case config::Route::BackdropKind::Screen: {
      // Cached layout: the backdrop route was laid out once; repainting it
      // costs draw calls, not measurement.
      const LayoutNode& tree = engine->LayoutRoute(document, found->backdrop_value, size, model);
      PaintTree(backend, tree);
      return;
    }
    case config::Route::BackdropKind::None:
      return;
  }
}

}  // namespace ui::layout

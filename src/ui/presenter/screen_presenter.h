// The JSON-to-pixels wire (#71): paints the resolved route through the
// render seam and owns the interactive half (focus ring, Tab traversal,
// click-to-focus). Action dispatch — what a button DOES — stays inert and
// declared-only until the module contract (Phase 3); that decision is
// recorded on issue #71.
//
// The shell calls Paint inside its frame scope; coordinates are translated
// so the route origin lands at the content rect's top-left.
#pragma once

#include <string>

#include "render/render_backend.h"
#include "ui/config/resolved_ui_document.h"
#include "ui/focus/focus_coordinator.h"
#include "ui/layout/layout_engine.h"

namespace ui::presenter {

class ScreenPresenter final {
 public:
  explicit ScreenPresenter(render::RenderBackend* backend, std::wstring theme = L"dark");

  // Rebuilds focus and drops layout memo for the old document; the current
  // route becomes the document's initial route.
  void SetDocument(const config::ResolvedUiDocument* document);

  const std::wstring& current_route() const { return route_; }
  void SwitchRoute(std::wstring_view route);

  // Frame-scope paint of backdrop + layout tree + focus ring.
  void Paint(const render::Rect& content);

  // VK_TAB / Shift+VK_TAB advance focus; returns true when handled.
  bool HandleKey(int virtual_key);
  // Click-to-focus on an id'd focusable node; true when focus moved.
  bool HandleClick(float x, float y);

  std::wstring focused() const { return focus_.Current(route_); }

 private:
  void PaintNode(const layout::LayoutNode& node);
  bool ClickNode(const layout::LayoutNode& node, float x, float y);
  render::Color Token(std::wstring_view name, render::Color fallback) const;

  render::RenderBackend* backend_;
  layout::LayoutEngine engine_;
  focus::FocusCoordinator focus_;
  const config::ResolvedUiDocument* document_ = nullptr;
  const layout::LayoutNode* last_tree_ = nullptr;
  std::wstring route_;
  std::wstring theme_;
};

}  // namespace ui::presenter

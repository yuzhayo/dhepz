// The JSON-to-pixels wire (#71): paints the resolved route through the
// render seam and owns the interactive half (focus ring, Tab traversal,
// click-to-focus). Actions leave through an injected callback; this layer
// never depends on the module gate or module contract.
//
// The shell calls Paint inside its frame scope; coordinates are translated
// so the route origin lands at the content rect's top-left.
#pragma once

#include <functional>
#include <string>

#include "render/render_backend.h"
#include "ui/config/resolved_ui_document.h"
#include "ui/focus/focus_coordinator.h"
#include "ui/layout/backdrop.h"
#include "ui/layout/layout_engine.h"

namespace ui::presenter {

class ScreenPresenter final {
 public:
  using ActionDispatchHandler = std::function<core::Status(
      std::wstring_view route, std::wstring_view action,
      const json::Value& payload, json::Value* state_patch)>;
  using RouteChangedHandler = std::function<void(std::wstring_view route)>;

  explicit ScreenPresenter(render::RenderBackend* backend, std::wstring theme = L"dark");

  // Rebuilds focus and drops layout memo for the old document; the current
  // route becomes the document's initial route.
  void SetDocument(const config::ResolvedUiDocument* document);

  const std::wstring& current_route() const { return route_; }
  void SwitchRoute(std::wstring_view route);

  // Measurement pass: layout the route (and a screen backdrop) OUTSIDE the
  // paint scope. The shell calls this before BeginFrame.
  void Prepare(const render::Rect& content);
  // Draw pass: paints the cached trees inside the frame scope.
  void Paint(const render::Rect& content);

  // VK_TAB / Shift+VK_TAB advance focus; returns true when handled.
  bool HandleKey(int virtual_key);
  // Hover and press feedback; true when the visual state changed.
  bool HandleMove(float x, float y);
  bool HandleDown(float x, float y);
  // Click-to-focus on a focusable node; true when focus moved.
  bool HandleClick(float x, float y);

  void set_action_dispatch_handler(ActionDispatchHandler handler) {
    action_dispatch_handler_ = std::move(handler);
  }
  void set_route_changed_handler(RouteChangedHandler handler) {
    route_changed_handler_ = std::move(handler);
  }
  core::Status ApplyStatePatch(std::wstring_view route,
                               const json::Value& patch);
  const json::Value* ViewStateValue(std::wstring_view route,
                                    std::wstring_view binding) const;
  const core::Status& last_action_status() const { return last_action_status_; }

  // The shell reserves its caption row for dragging and the main icons;
  // the tab strip sits below it. Default 0 keeps the strip at the top.
  void set_caption_height(float height) { caption_height_ = height; }

  // True when the point (content-relative) is over interactive content —
  // the shell uses it to keep such points out of the caption drag zone.
  bool HitTestContent(float x, float y) const;
  bool InteractiveBounds(std::wstring_view id, render::Rect* out) const;

  std::wstring focused() const { return focus_.Current(route_); }

 private:
  void PaintNode(const layout::LayoutNode& node);
  void PaintTabs();
  bool ClickNode(const layout::LayoutNode& node, float x, float y);
  const config::ComponentNode* FindButton(const layout::LayoutNode& node, float x,
                                          float y) const;
  const layout::LayoutNode* FindNodeById(const layout::LayoutNode& node,
                                         std::wstring_view id) const;
  int TabAt(float x, float y) const;
  render::Color Token(std::wstring_view name, render::Color fallback) const;
  const json::Value* ResolveBinding(std::wstring_view route,
                                    std::wstring_view binding) const;
  json::Value ResolveTemplate(const json::Value& value) const;
  std::wstring ResolvedString(const config::ComponentNode& node,
                              std::wstring_view property,
                              std::wstring_view fallback = {}) const;
  bool ResolvedBool(const config::ComponentNode& node,
                    std::wstring_view property, bool fallback = false) const;
  void InvalidateBoundNodes(const layout::LayoutNode& node,
                            const std::vector<std::wstring>& changed);
  // The style a text node is measured AND painted with; keeping the two
  // identical is what warms the painted font during layout.
  static render::TextStyle StyleForText(const config::ComponentNode& node);

  render::RenderBackend* backend_;
  layout::LayoutEngine engine_;
  focus::FocusCoordinator focus_;
  const config::ResolvedUiDocument* document_ = nullptr;
  const layout::LayoutNode* last_tree_ = nullptr;
  const layout::LayoutNode* backdrop_tree_ = nullptr;
  const config::ComponentNode* focused_node_ = nullptr;
  const config::ComponentNode* hover_node_ = nullptr;
  const config::ComponentNode* pressed_node_ = nullptr;
  std::vector<render::Rect> tab_rects_;
  std::vector<std::wstring> tab_routes_;
  std::vector<std::wstring> tab_labels_;
  int hover_tab_ = -1;
  int pressed_tab_ = -1;
  float caption_height_ = 0.0f;
  float tab_strip_height_ = 0.0f;
  layout::PaintPlan plan_;
  std::vector<std::pair<std::wstring, json::Value>> route_states_;
  ActionDispatchHandler action_dispatch_handler_;
  RouteChangedHandler route_changed_handler_;
  core::Status last_action_status_;
  render::Rect last_content_{};
  std::wstring route_;
  std::wstring theme_;
};

}  // namespace ui::presenter

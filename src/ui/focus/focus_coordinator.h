// Keyboard focus ownership (#55): traversal order, current owner per route,
// and restore-on-return policy, all as pure logic over the resolved tree.
// No Windows, no IO — nothing here can block the UI thread.
//
// Traversal order is declaration order (the resolver preserves member and
// children order). A node is focusable when its resolved properties say
// tab_stop, visible and enabled. Nodes without an explicit id get a
// deterministic synthetic one so traversal is still addressable.
#pragma once

#include <string>
#include <vector>

#include "ui/config/resolved_ui_document.h"

namespace ui::focus {

class FocusCoordinator final {
 public:
  // Rebuilds per-route traversal order; saved focus positions are dropped.
  void SetDocument(const config::ResolvedUiDocument* document);

  // Focusable ids in traversal order; empty route -> empty vector.
  std::vector<std::wstring> Focusables(std::wstring_view route) const;

  // The node a focusable id refers to; nullptr for unknown ids.
  const config::ComponentNode* NodeFor(std::wstring_view route, std::wstring_view id) const;

  // Current owner; empty when the route has nothing focusable or has not
  // been entered.
  std::wstring Current(std::wstring_view route) const;

  // Route-switch policy: restore the saved id when it still exists in the
  // traversal order, otherwise the first focusable.
  void EnterRoute(std::wstring_view route);

  // Explicit ownership; false when the id is not focusable on that route.
  bool SetFocus(std::wstring_view route, std::wstring_view id);

  // Tab / Shift+Tab with wrap-around; returns the new owner, empty when the
  // route has nothing focusable.
  std::wstring Advance(std::wstring_view route, bool backward);

 private:
  struct RouteState {
    std::vector<std::pair<std::wstring, const config::ComponentNode*>> order;
    std::wstring current;
  };
  RouteState* Find(std::wstring_view route);
  const RouteState* Find(std::wstring_view route) const;

  const config::ResolvedUiDocument* document_ = nullptr;
  std::vector<std::pair<std::wstring, RouteState>> routes_;
};

}  // namespace ui::focus

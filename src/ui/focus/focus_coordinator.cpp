#include "ui/focus/focus_coordinator.h"

#include <algorithm>

namespace ui::focus {
namespace {

void Collect(const config::ComponentNode& node, const std::wstring& prefix, int& counter,
             std::vector<std::wstring>* out) {
  const bool focusable = node.GetBool(L"tab_stop") && node.GetBool(L"visible", true) &&
                         node.GetBool(L"enabled", true);
  if (focusable) {
    if (!node.id().empty()) {
      out->push_back(node.id());
    } else {
      out->push_back(prefix + L"@" + std::to_wstring(counter));
    }
  }
  for (const config::ComponentNode& child : node.children()) {
    ++counter;
    Collect(child, prefix, counter, out);
  }
}

}  // namespace

void FocusCoordinator::SetDocument(const config::ResolvedUiDocument* document) {
  document_ = document;
  routes_.clear();
  if (document_ == nullptr) return;
  for (const config::Route& route : document_->routes()) {
    RouteState state;
    int counter = 0;
    Collect(route.root, route.id, counter, &state.order);
    routes_.emplace_back(route.id, std::move(state));
  }
}

std::vector<std::wstring> FocusCoordinator::Focusables(std::wstring_view route) const {
  const RouteState* state = Find(route);
  return state != nullptr ? state->order : std::vector<std::wstring>{};
}

std::wstring FocusCoordinator::Current(std::wstring_view route) const {
  const RouteState* state = Find(route);
  return state != nullptr ? state->current : std::wstring{};
}

void FocusCoordinator::EnterRoute(std::wstring_view route) {
  RouteState* state = Find(route);
  if (state == nullptr) return;
  const bool saved_exists = std::find(state->order.begin(), state->order.end(),
                                      state->current) != state->order.end();
  if (!saved_exists) {
    state->current = state->order.empty() ? std::wstring{} : state->order.front();
  }
}

bool FocusCoordinator::SetFocus(std::wstring_view route, std::wstring_view id) {
  RouteState* state = Find(route);
  if (state == nullptr) return false;
  const auto known = std::find(state->order.begin(), state->order.end(), id);
  if (known == state->order.end()) return false;
  state->current = *known;
  return true;
}

std::wstring FocusCoordinator::Advance(std::wstring_view route, bool backward) {
  RouteState* state = Find(route);
  if (state == nullptr || state->order.empty()) return {};
  const auto position =
      std::find(state->order.begin(), state->order.end(), state->current);
  std::size_t index;
  if (position == state->order.end()) {
    index = backward ? state->order.size() - 1 : 0;
  } else {
    const std::size_t current_index = static_cast<std::size_t>(position - state->order.begin());
    index = backward ? (current_index + state->order.size() - 1) % state->order.size()
                     : (current_index + 1) % state->order.size();
  }
  state->current = state->order[index];
  return state->current;
}

FocusCoordinator::RouteState* FocusCoordinator::Find(std::wstring_view route) {
  for (auto& [id, state] : routes_) {
    if (id == route) return &state;
  }
  return nullptr;
}

const FocusCoordinator::RouteState* FocusCoordinator::Find(std::wstring_view route) const {
  for (const auto& [id, state] : routes_) {
    if (id == route) return &state;
  }
  return nullptr;
}

}  // namespace ui::focus

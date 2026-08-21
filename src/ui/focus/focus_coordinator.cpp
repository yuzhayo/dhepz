#include "ui/focus/focus_coordinator.h"

#include <algorithm>

namespace ui::focus {
namespace {

void Collect(const config::ComponentNode& node, const std::wstring& prefix, int& counter,
             std::vector<std::pair<std::wstring, const config::ComponentNode*>>* out) {
  const bool focusable = node.GetBool(L"tab_stop") && node.GetBool(L"visible", true) &&
                         node.GetBool(L"enabled", true);
  if (focusable) {
    if (!node.id().empty()) {
      out->emplace_back(node.id(), &node);
    } else {
      out->emplace_back(prefix + L"@" + std::to_wstring(counter), &node);
    }
  }
  for (const config::ComponentNode& child : node.children()) {
    ++counter;
    Collect(child, prefix, counter, out);
  }
}

bool HasId(const std::vector<std::pair<std::wstring, const config::ComponentNode*>>& order,
           std::wstring_view id) {
  return std::find_if(order.begin(), order.end(), [id](const auto& entry) {
           return entry.first == id;
         }) != order.end();
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
  std::vector<std::wstring> ids;
  if (state != nullptr) {
    ids.reserve(state->order.size());
    for (const auto& [id, node] : state->order) {
      ids.push_back(id);
    }
  }
  return ids;
}

const config::ComponentNode* FocusCoordinator::NodeFor(std::wstring_view route,
                                                       std::wstring_view id) const {
  const RouteState* state = Find(route);
  if (state == nullptr) return nullptr;
  const auto entry = std::find_if(state->order.begin(), state->order.end(),
                                  [id](const auto& pair) { return pair.first == id; });
  return entry != state->order.end() ? entry->second : nullptr;
}

std::wstring FocusCoordinator::Current(std::wstring_view route) const {
  const RouteState* state = Find(route);
  return state != nullptr ? state->current : std::wstring{};
}

void FocusCoordinator::EnterRoute(std::wstring_view route) {
  RouteState* state = Find(route);
  if (state == nullptr) return;
  const bool saved_exists = !state->current.empty() && HasId(state->order, state->current);
  if (!saved_exists) {
    state->current = state->order.empty() ? std::wstring{} : state->order.front().first;
  }
}

bool FocusCoordinator::SetFocus(std::wstring_view route, std::wstring_view id) {
  RouteState* state = Find(route);
  if (state == nullptr || !HasId(state->order, id)) return false;
  state->current = std::wstring(id);
  return true;
}

std::wstring FocusCoordinator::Advance(std::wstring_view route, bool backward) {
  RouteState* state = Find(route);
  if (state == nullptr || state->order.empty()) return {};
  const std::wstring current = state->current;
  const auto position = std::find_if(state->order.begin(), state->order.end(),
                                     [&current](const auto& entry) {
                                       return entry.first == current;
                                     });
  std::size_t index;
  if (position == state->order.end()) {
    index = backward ? state->order.size() - 1 : 0;
  } else {
    const std::size_t current_index = static_cast<std::size_t>(position - state->order.begin());
    index = backward ? (current_index + state->order.size() - 1) % state->order.size()
                     : (current_index + 1) % state->order.size();
  }
  state->current = state->order[index].first;
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

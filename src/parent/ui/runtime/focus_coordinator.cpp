#include "parent/ui/runtime/focus_coordinator.h"

#include <algorithm>

namespace ui::focus {
namespace {

const layout::LayoutNode* FindDialog(const layout::LayoutNode& node) {
  for (auto child = node.children.rbegin(); child != node.children.rend(); ++child) {
    if (const layout::LayoutNode* found = FindDialog(*child)) return found;
  }
  return node.source != nullptr && !node.bounds.empty() && node.source->type() == L"dialog"
             ? &node
             : nullptr;
}

}  // namespace

FocusCoordinator::FocusCoordinator(const components::ComponentRegistry* registry)
    : registry_(registry) {}

void FocusCoordinator::Collect(const layout::LayoutNode& node) {
  if (node.source == nullptr || node.bounds.empty()) return;
  const components::ComponentDescriptor* descriptor = registry_->Find(node.source->type());
  if (descriptor != nullptr && descriptor->can_focus != nullptr &&
      descriptor->can_focus(*node.source)) {
    order_.push_back(node.source);
  }
  for (const layout::LayoutNode& child : node.children) Collect(child);
}

void FocusCoordinator::Clear() {
  order_.clear();
  index_ = kNoFocus;
}

void FocusCoordinator::Rebuild(const layout::LayoutNode* tree) {
  const config::ComponentNode* previous = current();
  order_.clear();
  index_ = kNoFocus;
  if (tree != nullptr) {
    const layout::LayoutNode* modal = FindDialog(*tree);
    Collect(modal != nullptr ? *modal : *tree);
  }
  if (previous != nullptr) Focus(previous);
}

const config::ComponentNode* FocusCoordinator::current() const {
  return order_.empty() || index_ == kNoFocus ? nullptr : order_[index_];
}

bool FocusCoordinator::Focus(const config::ComponentNode* node) {
  const auto found = std::find(order_.begin(), order_.end(), node);
  if (found == order_.end()) return false;
  index_ = static_cast<std::size_t>(found - order_.begin());
  return true;
}

bool FocusCoordinator::Advance(bool backward) {
  if (order_.empty()) return false;
  if (index_ == kNoFocus) {
    index_ = backward ? order_.size() - 1 : 0;
  } else {
    index_ = backward ? (index_ + order_.size() - 1) % order_.size()
                      : (index_ + 1) % order_.size();
  }
  return true;
}

}  // namespace ui::focus

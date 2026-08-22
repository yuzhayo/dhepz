#include "parent/ui/runtime/screen_presenter.h"

#include <windows.h>

namespace ui::presenter {

ScreenPresenter::ScreenPresenter(render::RenderBackend* backend, application::UiState* state,
                                 application::UiActionRegistry* actions)
    : backend_(backend),
      state_(state),
      actions_(actions),
      layout_(backend, &registry_, state),
      focus_(&registry_) {}

void ScreenPresenter::SetDocument(const config::ResolvedUiDocument* document) {
  document_ = document;
  route_ = document != nullptr ? document->initial_route() : std::wstring{};
  focus_.Clear();
  tree_ = nullptr;
  hovered_ = nullptr;
  pressed_ = nullptr;
  expanded_ = nullptr;
}

void ScreenPresenter::Prepare(render::Size size) {
  viewport_size_ = size;
  tree_ = document_ != nullptr ? &layout_.LayoutRoute(*document_, route_, size) : nullptr;
  focus_.Rebuild(tree_);
  if (expanded_ != nullptr && tree_ != nullptr && FindSource(*tree_, expanded_) == nullptr) {
    expanded_ = nullptr;
  }
}

void ScreenPresenter::Paint(const render::Rect& viewport) {
  if (tree_ == nullptr) return;
  backend_->PushTranslation({viewport.x, viewport.y});
  PaintNode(*tree_);
  if (expanded_ != nullptr) {
    const layout::LayoutNode* anchor = FindSource(*tree_, expanded_);
    const components::ComponentDescriptor* descriptor = registry_.Find(expanded_->type());
    if (anchor != nullptr && descriptor != nullptr && descriptor->paint_overlay != nullptr) {
      descriptor->paint_overlay(*expanded_, anchor->bounds, viewport_size_, palette_, *state_,
                                *backend_);
    }
  }
  backend_->PopTranslation();
}

void ScreenPresenter::PaintNode(const layout::LayoutNode& node) {
  if (node.source == nullptr || node.bounds.empty()) return;
  const components::ComponentDescriptor* descriptor = registry_.Find(node.source->type());
  if (descriptor != nullptr && descriptor->paint != nullptr) {
    components::ComponentVisualState visual;
    visual.hovered = node.source == hovered_;
    visual.pressed = node.source == pressed_;
    visual.focused = node.source == focus_.current();
    visual.enabled = node.source->GetBool(L"enabled", true);
    descriptor->paint(*node.source, node.bounds, visual, palette_, *state_, *backend_);
  }
  if (node.clip_children) backend_->PushClip(node.bounds);
  for (const layout::LayoutNode& child : node.children) PaintNode(child);
  if (node.clip_children) backend_->PopClip();
}

const layout::LayoutNode* ScreenPresenter::Hit(const layout::LayoutNode& node, float x,
                                                float y) const {
  if (node.source == nullptr || node.bounds.empty()) return nullptr;
  if (node.clip_children && !node.bounds.contains({x, y})) return nullptr;
  for (auto child = node.children.rbegin(); child != node.children.rend(); ++child) {
    if (const layout::LayoutNode* hit = Hit(*child, x, y)) return hit;
  }
  if (!node.bounds.contains({x, y}) || !node.source->GetBool(L"enabled", true)) return nullptr;
  const components::ComponentDescriptor* descriptor = registry_.Find(node.source->type());
  if (descriptor == nullptr) return nullptr;
  const bool focusable = descriptor->can_focus != nullptr && descriptor->can_focus(*node.source);
  return focusable || descriptor->activate != nullptr || descriptor->pointer != nullptr ||
                 descriptor->paint_overlay != nullptr
             ? &node
             : nullptr;
}

const layout::LayoutNode* ScreenPresenter::FindWheel(const layout::LayoutNode& node, float x,
                                                      float y) const {
  if (node.source == nullptr || node.bounds.empty() || !node.bounds.contains({x, y})) {
    return nullptr;
  }
  for (auto child = node.children.rbegin(); child != node.children.rend(); ++child) {
    if (const layout::LayoutNode* found = FindWheel(*child, x, y)) return found;
  }
  const components::ComponentDescriptor* descriptor = registry_.Find(node.source->type());
  return descriptor != nullptr && descriptor->wheel != nullptr ? &node : nullptr;
}

const layout::LayoutNode* ScreenPresenter::FindSource(
    const layout::LayoutNode& node, const config::ComponentNode* source) const {
  if (node.source == source) return &node;
  for (const layout::LayoutNode& child : node.children) {
    if (const layout::LayoutNode* found = FindSource(child, source)) return found;
  }
  return nullptr;
}

const layout::LayoutNode* ScreenPresenter::FindDialog(const layout::LayoutNode& node) const {
  for (auto child = node.children.rbegin(); child != node.children.rend(); ++child) {
    if (const layout::LayoutNode* found = FindDialog(*child)) return found;
  }
  return node.source != nullptr && !node.bounds.empty() && node.source->type() == L"dialog"
             ? &node
             : nullptr;
}

bool ScreenPresenter::HandleMove(float x, float y) {
  const layout::LayoutNode* hit = tree_ != nullptr ? Hit(*tree_, x, y) : nullptr;
  const config::ComponentNode* next = hit != nullptr ? hit->source : nullptr;
  if (next == hovered_) return false;
  hovered_ = next;
  return true;
}

bool ScreenPresenter::HandleDown(float x, float y) {
  if (expanded_ != nullptr) {
    pressed_ = nullptr;
    return true;
  }
  const layout::LayoutNode* hit = tree_ != nullptr ? Hit(*tree_, x, y) : nullptr;
  const config::ComponentNode* next = hit != nullptr ? hit->source : nullptr;
  const bool changed = pressed_ != next;
  pressed_ = next;
  if (next != nullptr) focus_.Focus(next);
  return changed || next != nullptr;
}

bool ScreenPresenter::Dispatch(components::ComponentResult result) {
  if (!result.handled && result.patch.empty() && result.event.action.empty()) return false;
  bool changed = state_->Apply(result.patch);
  if (!result.event.action.empty() && actions_ != nullptr) {
    const application::UiPatch action_patch = actions_->Dispatch(result.event, *state_);
    changed = state_->Apply(action_patch) || changed;
    if (!action_patch.route.empty() && document_ != nullptr &&
        document_->FindRoute(action_patch.route) != nullptr) {
      route_ = action_patch.route;
      tree_ = nullptr;
      focus_.Clear();
      hovered_ = nullptr;
      pressed_ = nullptr;
      expanded_ = nullptr;
      changed = true;
    }
  }
  return result.handled || changed;
}

bool ScreenPresenter::Activate(const config::ComponentNode* node) {
  if (node == nullptr) return false;
  const components::ComponentDescriptor* descriptor = registry_.Find(node->type());
  if (descriptor == nullptr || descriptor->activate == nullptr) return false;
  return Dispatch(descriptor->activate(*node, *state_));
}

bool ScreenPresenter::HandleClick(float x, float y) {
  if (tree_ == nullptr) return false;
  if (expanded_ != nullptr) {
    const layout::LayoutNode* anchor = FindSource(*tree_, expanded_);
    const components::ComponentDescriptor* descriptor = registry_.Find(expanded_->type());
    if (anchor != nullptr && descriptor != nullptr && descriptor->overlay_pointer != nullptr) {
      Dispatch(descriptor->overlay_pointer(*expanded_, *state_, {x, y}, anchor->bounds,
                                           viewport_size_));
    }
    expanded_ = nullptr;
    hovered_ = nullptr;
    pressed_ = nullptr;
    return true;
  }

  if (const layout::LayoutNode* dialog = FindDialog(*tree_)) {
    const components::ComponentDescriptor* descriptor = registry_.Find(dialog->source->type());
    if (descriptor != nullptr && descriptor->pointer != nullptr) {
      const components::ComponentResult result =
          descriptor->pointer(*dialog->source, *state_, {x, y}, dialog->bounds);
      if (result.handled || !result.patch.empty() || !result.event.action.empty()) {
        pressed_ = nullptr;
        return Dispatch(result);
      }
    }
  }

  const layout::LayoutNode* hit = Hit(*tree_, x, y);
  const config::ComponentNode* released = hit != nullptr ? hit->source : nullptr;
  const config::ComponentNode* pressed = pressed_;
  pressed_ = nullptr;
  if (released == nullptr || released != pressed || hit == nullptr) return pressed != nullptr;
  const components::ComponentDescriptor* descriptor = registry_.Find(released->type());
  if (descriptor == nullptr) return false;
  if (descriptor->paint_overlay != nullptr) {
    expanded_ = released;
    return true;
  }
  if (descriptor->pointer != nullptr) {
    return Dispatch(descriptor->pointer(*released, *state_, {x, y}, hit->bounds));
  }
  return Activate(released);
}

bool ScreenPresenter::HandleKey(int virtual_key) {
  if (expanded_ != nullptr && virtual_key == VK_ESCAPE) {
    expanded_ = nullptr;
    return true;
  }
  if (tree_ != nullptr && virtual_key == VK_ESCAPE) {
    if (const layout::LayoutNode* dialog = FindDialog(*tree_)) {
      const components::ComponentDescriptor* descriptor = registry_.Find(dialog->source->type());
      if (descriptor != nullptr && descriptor->key != nullptr &&
          Dispatch(descriptor->key(*dialog->source, *state_, virtual_key))) {
        return true;
      }
    }
  }
  if (virtual_key == VK_TAB) {
    const bool backward = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    return focus_.Advance(backward);
  }
  const config::ComponentNode* current = focus_.current();
  const components::ComponentDescriptor* descriptor =
      current != nullptr ? registry_.Find(current->type()) : nullptr;
  if (descriptor != nullptr && descriptor->key != nullptr &&
      Dispatch(descriptor->key(*current, *state_, virtual_key))) {
    return true;
  }
  if (descriptor != nullptr && descriptor->paint_overlay != nullptr &&
      (virtual_key == VK_RETURN || virtual_key == VK_SPACE)) {
    expanded_ = expanded_ == current ? nullptr : current;
    return true;
  }
  if (virtual_key == VK_RETURN || virtual_key == VK_SPACE) return Activate(current);
  return false;
}

bool ScreenPresenter::HandleText(wchar_t character) {
  const config::ComponentNode* current = focus_.current();
  const components::ComponentDescriptor* descriptor =
      current != nullptr ? registry_.Find(current->type()) : nullptr;
  return descriptor != nullptr && descriptor->text_input != nullptr
             ? Dispatch(descriptor->text_input(*current, *state_, character))
             : false;
}

bool ScreenPresenter::HandleWheel(float x, float y, int delta) {
  if (tree_ == nullptr) return false;
  const layout::LayoutNode* target = FindWheel(*tree_, x, y);
  if (target == nullptr || target->source == nullptr) return false;
  const components::ComponentDescriptor* descriptor = registry_.Find(target->source->type());
  return descriptor != nullptr && descriptor->wheel != nullptr
             ? Dispatch(descriptor->wheel(*target->source, *state_, delta))
             : false;
}

}  // namespace ui::presenter

#include "parent/ui/runtime/screen_presenter.h"

#include <algorithm>
#include <utility>
#include <windows.h>

#include "ui/components/component_helpers.h"

namespace ui::presenter {

ScreenPresenter::ScreenPresenter(render::RenderBackend* backend, application::UiState* state,
                                 application::UiActionRegistry* actions)
    : backend_(backend),
      state_(state),
      actions_(actions),
      layout_(backend, &registry_, state),
      backdrop_layout_(backend, &registry_, state),
      focus_(&registry_) {
  tab_node_.SetProperty(L"routes_binding", json::Value::String(L"parent.tabs.routes"));
  tab_node_.SetProperty(L"labels_binding", json::Value::String(L"parent.tabs.labels"));
  tab_node_.SetProperty(L"selected_binding", json::Value::String(L"parent.tabs.selected"));
  tab_node_.SetProperty(L"locked_binding", json::Value::String(L"parent.tabs.locked"));
  tab_node_.SetProperty(L"multi_row_binding", json::Value::String(L"parent.tabs.multi_row"));
  tab_node_.SetProperty(L"select_action", json::Value::String(L"parent.tabs.select"));
  tab_node_.SetProperty(L"reorder_action", json::Value::String(L"parent.tabs.reorder"));
  tab_node_.SetProperty(L"lock_action", json::Value::String(L"parent.tabs.lock"));
}

void ScreenPresenter::SetDocument(const config::ResolvedUiDocument* document) {
  document_ = document;
  route_ = document != nullptr
               ? state_->Text(L"parent.tabs.selected", document->initial_route())
               : std::wstring{};
  if (document_ != nullptr && document_->FindRoute(route_) == nullptr) {
    route_ = document_->initial_route();
  }
  focus_.Clear();
  tree_ = nullptr;
  hovered_ = nullptr;
  pressed_ = nullptr;
  expanded_ = nullptr;
  tab_pressed_ = false;
  ApplyDocumentPalette();
}

void ScreenPresenter::ApplyDocumentPalette() {
  if (document_ == nullptr) return;
  auto assign = [this](std::wstring_view name, render::Color* target) {
    config::Rgba value;
    if (document_->Token(L"dark", name, &value)) {
      *target = {value.r, value.g, value.b, value.a};
    }
  };
  assign(L"surface", &palette_.surface);
  assign(L"surface_alt", &palette_.control);
  assign(L"border", &palette_.border);
  assign(L"text", &palette_.text);
  assign(L"accent", &palette_.focus);
  assign(L"danger", &palette_.danger);
  auto lighter = [](render::Color source, int amount) {
    const auto channel = [amount](unsigned char value) {
      return static_cast<unsigned char>(std::min(255, static_cast<int>(value) + amount));
    };
    return render::Color{channel(source.r), channel(source.g), channel(source.b), source.a};
  };
  palette_.control_hover = lighter(palette_.control, 14);
  palette_.control_pressed = lighter(palette_.control, 28);
}

void ScreenPresenter::Prepare(render::Size size) {
  content_offset_y_ = 0.0f;
  tab_bounds_ = {};
  if (HasTabs()) {
    const components::ComponentDescriptor* tabs = registry_.Find(L"tabs");
    if (tabs != nullptr && tabs->measure != nullptr) {
      const render::Size measured = tabs->measure(tab_node_, *backend_, *state_, size.width);
      content_offset_y_ = std::min(size.height, measured.height);
      tab_bounds_ = {0.0f, 0.0f, size.width, content_offset_y_};
    }
  }
  viewport_size_ = {size.width, std::max(0.0f, size.height - content_offset_y_)};
  tree_ = document_ != nullptr
              ? &layout_.LayoutRoute(*document_, route_, viewport_size_)
              : nullptr;
  focus_.Rebuild(tree_);
  if (expanded_ != nullptr && tree_ != nullptr && FindSource(*tree_, expanded_) == nullptr) {
    expanded_ = nullptr;
  }
}

void ScreenPresenter::Paint(const render::Rect& viewport) {
  if (tree_ == nullptr) return;
  backend_->PushTranslation({viewport.x, viewport.y});
  if (HasTabs()) {
    const components::ComponentDescriptor* tabs = registry_.Find(L"tabs");
    if (tabs != nullptr && tabs->paint != nullptr) {
      tabs->paint(tab_node_, tab_bounds_, {}, palette_, *state_, *backend_);
    }
  }
  backend_->PushTranslation({0.0f, content_offset_y_});
  PaintBackdrop();
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
  backend_->PopTranslation();
}

bool ScreenPresenter::HasTabs() const {
  const auto* routes = state_ != nullptr ? state_->Strings(L"parent.tabs.routes") : nullptr;
  return routes != nullptr && !routes->empty();
}

render::Point ScreenPresenter::ContentPoint(float x, float y) const {
  return {x, y - content_offset_y_};
}

bool ScreenPresenter::DispatchTabPointer(components::PointerLifecycleFn handler, float x,
                                         float y) {
  return handler != nullptr
             ? Dispatch(handler(tab_node_, *state_, {x, y}, tab_bounds_))
             : false;
}

void ScreenPresenter::PaintBackdrop() {
  if (document_ == nullptr) return;
  const config::Route* route = document_->FindRoute(route_);
  if (route == nullptr || route->backdrop_kind == config::Route::BackdropKind::None) return;
  const render::Rect full{0.0f, 0.0f, viewport_size_.width, viewport_size_.height};
  if (route->backdrop_kind == config::Route::BackdropKind::Color) {
    config::Rgba color;
    if (document_->Token(L"dark", route->backdrop_value, &color)) {
      backend_->FillRect(full, {color.r, color.g, color.b, color.a});
    }
    return;
  }
  if (route->backdrop_kind == config::Route::BackdropKind::Image) {
    const render::ImageHandle image = backend_->LoadImageFile(route->backdrop_value);
    if (image != render::ImageHandle::Invalid) {
      backend_->DrawImage(image, full, 1.0f);
      backend_->ReleaseImage(image);
    }
    return;
  }
  if (route->backdrop_kind == config::Route::BackdropKind::Screen) {
    const layout::LayoutNode& backdrop =
        backdrop_layout_.LayoutRoute(*document_, route->backdrop_value, viewport_size_);
    PaintNode(backdrop);
  }
}

void ScreenPresenter::PaintNode(const layout::LayoutNode& node) {
  if (node.source == nullptr || node.bounds.empty()) return;
  const components::ComponentDescriptor* descriptor = registry_.Find(node.source->type());
  if (descriptor != nullptr && descriptor->paint != nullptr) {
    components::ComponentVisualState visual;
    visual.hovered = node.source == hovered_;
    visual.pressed = node.source == pressed_;
    visual.focused = node.source == focus_.current();
    visual.enabled = components::BoundBool(*node.source, L"enabled_binding", *state_,
                                           node.source->GetBool(L"enabled", true));
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
  if (!node.bounds.contains({x, y}) ||
      !components::BoundBool(*node.source, L"enabled_binding", *state_,
                             node.source->GetBool(L"enabled", true))) return nullptr;
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
  const components::ComponentDescriptor* tabs = registry_.Find(L"tabs");
  if (HasTabs() && (tab_pressed_ || tab_bounds_.contains({x, y}))) {
    const bool content_hover_cleared = hovered_ != nullptr;
    hovered_ = nullptr;
    return DispatchTabPointer(tabs != nullptr ? tabs->pointer_move : nullptr, x, y) ||
           content_hover_cleared;
  }
  bool tab_hover_cleared = false;
  if (HasTabs() && state_->Integer(L"parent.tabs.hovered_index", -1) != -1) {
    tab_hover_cleared =
        DispatchTabPointer(tabs != nullptr ? tabs->pointer_move : nullptr, -1.0f, -1.0f);
  }
  const render::Point point = ContentPoint(x, y);
  const layout::LayoutNode* hit = tree_ != nullptr ? Hit(*tree_, point.x, point.y) : nullptr;
  const config::ComponentNode* next = hit != nullptr ? hit->source : nullptr;
  if (next == hovered_) return tab_hover_cleared;
  hovered_ = next;
  return true;
}

bool ScreenPresenter::HandleDown(float x, float y) {
  if (HasTabs() && tab_bounds_.contains({x, y})) {
    const components::ComponentDescriptor* tabs = registry_.Find(L"tabs");
    tab_pressed_ = DispatchTabPointer(tabs != nullptr ? tabs->pointer_down : nullptr, x, y);
    return tab_pressed_;
  }
  const render::Point point = ContentPoint(x, y);
  if (expanded_ != nullptr) {
    const layout::LayoutNode* hit = tree_ != nullptr ? Hit(*tree_, point.x, point.y) : nullptr;
    pressed_ = hit != nullptr ? hit->source : nullptr;
    return true;
  }
  const layout::LayoutNode* hit = tree_ != nullptr ? Hit(*tree_, point.x, point.y) : nullptr;
  const config::ComponentNode* next = hit != nullptr ? hit->source : nullptr;
  const bool changed = pressed_ != next;
  pressed_ = next;
  if (next != nullptr) {
    focus_.Focus(next);
  } else {
    focus_.Blur();
  }
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
      state_->Set(L"parent.tabs.selected", route_);
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
  if (tab_pressed_) {
    tab_pressed_ = false;
    const components::ComponentDescriptor* tabs = registry_.Find(L"tabs");
    return DispatchTabPointer(tabs != nullptr ? tabs->pointer_up : nullptr, x, y);
  }
  if (tree_ == nullptr) return false;
  const render::Point point = ContentPoint(x, y);
  bool dismissed_overlay = false;
  if (expanded_ != nullptr) {
    const layout::LayoutNode* anchor = FindSource(*tree_, expanded_);
    const components::ComponentDescriptor* descriptor = registry_.Find(expanded_->type());
    components::ComponentResult overlay_result;
    if (anchor != nullptr && descriptor != nullptr && descriptor->overlay_pointer != nullptr) {
      overlay_result = descriptor->overlay_pointer(*expanded_, *state_, point,
                                                   anchor->bounds, viewport_size_);
    }
    expanded_ = nullptr;
    hovered_ = nullptr;
    dismissed_overlay = true;
    if (overlay_result.handled || !overlay_result.patch.empty() ||
        !overlay_result.event.action.empty()) {
      pressed_ = nullptr;
      return Dispatch(std::move(overlay_result));
    }
  }

  if (const layout::LayoutNode* dialog = FindDialog(*tree_)) {
    const components::ComponentDescriptor* descriptor = registry_.Find(dialog->source->type());
    if (descriptor != nullptr && descriptor->pointer != nullptr) {
      const components::ComponentResult result =
          descriptor->pointer(*dialog->source, *state_, point, dialog->bounds,
                              *backend_);
      if (result.handled || !result.patch.empty() || !result.event.action.empty()) {
        pressed_ = nullptr;
        return Dispatch(result);
      }
    }
  }

  const layout::LayoutNode* hit = Hit(*tree_, point.x, point.y);
  const config::ComponentNode* released = hit != nullptr ? hit->source : nullptr;
  const config::ComponentNode* pressed = pressed_;
  pressed_ = nullptr;
  if (released == nullptr || released != pressed || hit == nullptr) {
    return dismissed_overlay || pressed != nullptr;
  }
  const components::ComponentDescriptor* descriptor = registry_.Find(released->type());
  if (descriptor == nullptr) return dismissed_overlay;
  const bool has_overlay = descriptor->paint_overlay != nullptr &&
                           (descriptor->has_overlay == nullptr ||
                            descriptor->has_overlay(*released, *state_));
  if (has_overlay) {
    if (descriptor->pointer != nullptr) {
      Dispatch(descriptor->pointer(*released, *state_, point, hit->bounds,
                                   *backend_));
    }
    expanded_ = released;
    return true;
  }
  if (descriptor->pointer != nullptr) {
    return Dispatch(descriptor->pointer(*released, *state_, point, hit->bounds,
                                        *backend_)) ||
           dismissed_overlay;
  }
  return Activate(released) || dismissed_overlay;
}

bool ScreenPresenter::HandleDoubleClick(float x, float y) {
  const render::Point point = ContentPoint(x, y);
  const layout::LayoutNode* hit = tree_ != nullptr ? Hit(*tree_, point.x, point.y) : nullptr;
  if (hit == nullptr || hit->source == nullptr) return false;
  focus_.Focus(hit->source);
  const components::ComponentDescriptor* descriptor = registry_.Find(hit->source->type());
  return descriptor != nullptr && descriptor->double_click != nullptr
             ? Dispatch(descriptor->double_click(*hit->source, *state_))
             : false;
}

bool ScreenPresenter::HandleContext(float x, float y, void* owner_window) {
  const config::ComponentNode* target = nullptr;
  if (x >= 0.0f && y >= 0.0f && tree_ != nullptr) {
    const render::Point point = ContentPoint(x, y);
    const layout::LayoutNode* hit = Hit(*tree_, point.x, point.y);
    target = hit != nullptr ? hit->source : nullptr;
  } else {
    target = focus_.current();
  }
  if (target == nullptr) return false;
  focus_.Focus(target);
  expanded_ = nullptr;
  const components::ComponentDescriptor* descriptor = registry_.Find(target->type());
  return descriptor != nullptr && descriptor->context_menu != nullptr
             ? Dispatch(descriptor->context_menu(*target, *state_, owner_window))
             : false;
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
      (descriptor->has_overlay == nullptr || descriptor->has_overlay(*current, *state_)) &&
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
  const render::Point point = ContentPoint(x, y);
  const layout::LayoutNode* target = FindWheel(*tree_, point.x, point.y);
  if (target == nullptr || target->source == nullptr) return false;
  const components::ComponentDescriptor* descriptor = registry_.Find(target->source->type());
  return descriptor != nullptr && descriptor->wheel != nullptr
             ? Dispatch(descriptor->wheel(*target->source, *state_, delta))
             : false;
}

}  // namespace ui::presenter

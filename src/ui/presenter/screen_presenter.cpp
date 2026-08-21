#include "ui/presenter/screen_presenter.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>

#include "ui/layout/backdrop.h"

namespace ui::presenter {

ScreenPresenter::ScreenPresenter(render::RenderBackend* backend, std::wstring theme)
    : backend_(backend), engine_(backend), theme_(std::move(theme)) {
  engine_.set_text_style_provider(&ScreenPresenter::StyleForText);
}

render::TextStyle ScreenPresenter::StyleForText(const config::ComponentNode& node) {
  render::TextStyle style;
  const std::wstring variant = node.GetString(L"variant");
  if (variant == L"title") {
    style.size_px = 20.0f;
    style.weight = render::FontWeight::Semibold;
  } else if (variant == L"caption") {
    style.size_px = 12.0f;
  } else if (variant == L"monospace") {
    style.family = L"Cascadia Mono";
  }
  return style;
}

namespace {
render::Color Shade(render::Color color, int delta) {
  auto clamp = [](int value) {
    return static_cast<unsigned char>(value < 0 ? 0 : (value > 255 ? 255 : value));
  };
  return render::Color{clamp(color.r + delta), clamp(color.g + delta),
                       clamp(color.b + delta), color.a};
}

bool IsBindingReference(const json::Value& value, std::wstring* name = nullptr) {
  if (!value.is_object() || value.members().size() != 1) return false;
  const auto& [key, binding] = value.members().front();
  if (key != L"$bind" || !binding.is_string() || binding.AsString().empty()) {
    return false;
  }
  if (name != nullptr) *name = binding.AsString();
  return true;
}

bool ReferencesAny(const json::Value& value,
                   const std::vector<std::wstring>& changed) {
  std::wstring binding;
  if (IsBindingReference(value, &binding)) {
    const std::size_t dot = binding.find(L'.');
    return std::find(changed.begin(), changed.end(), binding.substr(0, dot)) !=
           changed.end();
  }
  if (value.is_object()) {
    for (const auto& [key, member] : value.members()) {
      (void)key;
      if (ReferencesAny(member, changed)) return true;
    }
  } else if (value.is_array()) {
    for (const json::Value& item : value.items()) {
      if (ReferencesAny(item, changed)) return true;
    }
  }
  return false;
}

bool NamedBindingReferences(std::wstring_view property,
                            const json::Value& value,
                            const std::vector<std::wstring>& changed) {
  if (property != L"value_binding" && property != L"items_binding" &&
      property != L"suggestions_binding" &&
      property != L"selected_value_binding" &&
      property != L"checked_binding" &&
      property != L"selected_id_binding") {
    return false;
  }
  if (!value.is_string()) return false;
  const std::wstring& binding = value.AsString();
  const std::size_t dot = binding.find(L'.');
  return std::find(changed.begin(), changed.end(), binding.substr(0, dot)) !=
         changed.end();
}

void MergeObject(json::Value* target, const json::Value& patch) {
  for (const auto& [key, value] : patch.members()) {
    if (value.is_null()) {
      target->Remove(key);
    } else if (value.is_object()) {
      json::Value* current = target->Find(key);
      if (current == nullptr || !current->is_object()) {
        target->Set(key, json::Value::Object());
        current = target->Find(key);
      }
      MergeObject(current, value);
    } else {
      target->Set(key, value);
    }
  }
}
}  // namespace

void ScreenPresenter::SetDocument(const config::ResolvedUiDocument* document) {
  document_ = document;
  focus_.SetDocument(document);
  route_.clear();
  suggestion_input_ = nullptr;
  suggestion_rects_.clear();
  suggestion_values_.clear();
  if (document_ != nullptr) {
    route_ = document_->initial_route();
    focus_.EnterRoute(route_);
  }
  backend_->InvalidateAll();
}

void ScreenPresenter::SwitchRoute(std::wstring_view route) {
  if (document_ == nullptr || document_->FindRoute(route) == nullptr) return;
  if (route_ == route) return;
  route_ = std::wstring(route);
  focus_.EnterRoute(route_);
  backend_->InvalidateAll();
  if (route_changed_handler_) route_changed_handler_(route_);
}

render::Color ScreenPresenter::Token(std::wstring_view name, render::Color fallback) const {
  config::Rgba rgba{};
  if (document_ != nullptr && document_->Token(theme_, name, &rgba)) {
    return render::Color{rgba.r, rgba.g, rgba.b, rgba.a};
  }
  return fallback;
}

void ScreenPresenter::Prepare(const render::Rect& content) {
  if (document_ == nullptr || route_.empty()) return;
  last_content_ = content;

  // Tab strip: one tab per route that shows in tabs, anchored left.
  tab_rects_.clear();
  tab_routes_.clear();
  tab_labels_.clear();
  float cursor_x = 12.0f;  // left padding: tabs never touch the border
  for (const config::Route& route : document_->routes()) {
    if (!route.show_in_tabs) continue;
    const std::wstring label = route.tab_label.empty() ? route.id : route.tab_label;
    const render::Size size = backend_->MeasureText(label, render::TextStyle{}, 0.0f);
    tab_rects_.push_back({cursor_x, caption_height_, size.width + 20.0f, 28.0f});
    tab_routes_.push_back(route.id);
    tab_labels_.push_back(label);
    cursor_x += size.width + 28.0f;
  }
  // Content always begins below the draggable caption. Routes with tabs add
  // one strip beneath it; a tabless single-feature route does not draw one.
  tab_strip_height_ =
      caption_height_ + (tab_rects_.empty() ? 0.0f : 36.0f);

  const render::Size size{content.width, content.height - tab_strip_height_};
  const config::Route* route = document_->FindRoute(route_);
  backdrop_tree_ = nullptr;
  if (route != nullptr &&
      route->backdrop_kind == config::Route::BackdropKind::Screen) {
    backdrop_tree_ =
        &engine_.LayoutRoute(*document_, route->backdrop_value, size, nullptr);
  }
  last_tree_ = &engine_.LayoutRoute(*document_, route_, size, nullptr);
  plan_ = layout::MakePaintPlan(*document_, route_);
}

void ScreenPresenter::Paint(const render::Rect& content) {
  if (document_ == nullptr || last_tree_ == nullptr) return;

  focused_node_ = focus_.NodeFor(route_, focus_.Current(route_));
  backend_->PushTranslation({content.x, content.y});
  switch (plan_.kind) {
    case config::Route::BackdropKind::Color: {
      config::Rgba rgba{};
      if (document_->Token(theme_, plan_.value, &rgba)) {
        backend_->FillRect({0.0f, 0.0f, content.width, content.height},
                           render::Color{rgba.r, rgba.g, rgba.b, rgba.a});
      }
      break;
    }
    case config::Route::BackdropKind::Image: {
      const render::ImageHandle image = backend_->LoadImageFile(plan_.value);
      if (image != render::ImageHandle::Invalid) {
        backend_->DrawImage(image, {0.0f, 0.0f, content.width, content.height}, 1.0f);
        backend_->ReleaseImage(image);
      }
      break;
    }
    case config::Route::BackdropKind::Screen:
      if (backdrop_tree_ != nullptr) {
        layout::PaintBackdropTree(backend_, *backdrop_tree_);
      }
      break;
    case config::Route::BackdropKind::None:
      break;
  }
  PaintTabs();
  backend_->PopTranslation();

  backend_->PushTranslation({content.x, content.y + tab_strip_height_});
  PaintNode(*last_tree_);
  PaintSuggestions();
  backend_->PopTranslation();
}

int ScreenPresenter::TabAt(float x, float y) const {
  for (std::size_t i = 0; i < tab_rects_.size(); ++i) {
    if (tab_rects_[i].contains({x, y})) return static_cast<int>(i);
  }
  return -1;
}

bool ScreenPresenter::HitTestContent(float x, float y) const {
  if (y >= caption_height_ && y < tab_strip_height_) return TabAt(x, y) >= 0;
  if (SuggestionAt(x, y - tab_strip_height_) >= 0) return true;
  return last_tree_ != nullptr &&
         FindButton(*last_tree_, x, y - tab_strip_height_) != nullptr;
}

void ScreenPresenter::PaintTabs() {
  for (std::size_t i = 0; i < tab_rects_.size(); ++i) {
    render::Rect box = tab_rects_[i];
    const bool selected = tab_routes_[i] == route_;
    render::Color outline = Token(L"borderStrong", {70, 76, 88, 255});
    const bool hovered = static_cast<int>(i) == hover_tab_;
    if (selected) {
      outline = Token(L"accent", {96, 165, 250, 255});
      render::Color tint = outline;
      tint.a = 90;
      backend_->FillRoundedRect(box, render::CornerRadius::Uniform(6.0f), tint);
    } else if (hovered) {
      outline = Token(L"accent", {96, 165, 250, 255});
    }
    if (static_cast<int>(i) == pressed_tab_) {
      outline = Shade(outline, -20);
      box.y += 1.0f;
    }
    backend_->StrokeRoundedRect(box, render::CornerRadius::Uniform(6.0f), outline, 1.0f);
    if (hovered) {
      const render::Rect inner{box.x + 1.0f, box.y + 1.0f, box.width - 2.0f,
                               box.height - 2.0f};
      backend_->StrokeRoundedRect(inner, render::CornerRadius::Uniform(5.0f), outline,
                                  2.0f);
    }
    backend_->DrawTextRun(tab_labels_[i], box, render::TextStyle{},
                          Token(L"text", {255, 255, 255, 255}), render::TextAlign::Center,
                          render::VerticalAlign::Middle);
  }
}

bool ScreenPresenter::ClickNode(const layout::LayoutNode& node, float x, float y) {
  for (const layout::LayoutNode& child : node.children) {
    if (ClickNode(child, x, y)) return true;
  }
  const config::ComponentNode* source = node.source;
  if (source == nullptr) return false;
  if (!node.bounds.contains({x, y})) return false;
  if (!(source->GetBool(L"tab_stop") && ResolvedBool(*source, L"visible", true) &&
        ResolvedBool(*source, L"enabled", true))) {
    return false;
  }
  const std::wstring id = focus_.IdFor(route_, source);
  bool handled = !id.empty() && focus_.SetFocus(route_, id);
  const std::wstring& type = source->type();
  if (type == L"input" && source->Property(L"suggestions_binding") != nullptr) {
    RebuildSuggestions(source, node.bounds);
  } else if (type == L"combo") {
    const json::Value* items_binding = source->Property(L"items_binding");
    const json::Value* selected_binding =
        source->Property(L"selected_value_binding");
    if (items_binding != nullptr && items_binding->is_string() &&
        selected_binding != nullptr && selected_binding->is_string()) {
      const json::Value* items = ResolveBinding(route_, items_binding->AsString());
      if (items != nullptr && items->is_array() && !items->items().empty()) {
        std::wstring selected;
        if (const json::Value* current =
                ResolveBinding(route_, selected_binding->AsString());
            current != nullptr && current->is_string()) {
          selected = current->AsString();
        }
        std::size_t next = 0;
        for (std::size_t i = 0; i < items->items().size(); ++i) {
          if (items->items()[i].is_string() &&
              items->items()[i].AsString() == selected) {
            next = (i + 1) % items->items().size();
            break;
          }
        }
        if (items->items()[next].is_string()) {
          json::Value patch = json::Value::Object();
          patch.Set(selected_binding->AsString(), items->items()[next]);
          handled = ApplyStatePatch(route_, patch).ok() || handled;
        }
      }
    }
  } else if (type == L"toggle" || type == L"checkbox") {
    const json::Value* checked_binding = source->Property(L"checked_binding");
    if (checked_binding != nullptr && checked_binding->is_string()) {
      bool checked = false;
      if (const json::Value* current =
              ResolveBinding(route_, checked_binding->AsString());
          current != nullptr && current->is_bool()) {
        checked = current->AsBool();
      }
      json::Value patch = json::Value::Object();
      patch.Set(checked_binding->AsString(), json::Value::Bool(!checked));
      handled = ApplyStatePatch(route_, patch).ok() || handled;
    }
  }
  const std::wstring action = source->GetString(L"action");
  if (!action.empty() && action_dispatch_handler_) {
    json::Value payload = json::Value::Object();
    if (const json::Value* declared = source->Property(L"action_payload")) {
      payload = ResolveTemplate(*declared);
    }
    json::Value patch;
    last_action_status_ =
        action_dispatch_handler_(route_, action, payload, &patch);
    if (patch.is_object()) (void)ApplyStatePatch(route_, patch);
    handled = true;
  }
  return handled;
}

const config::ComponentNode* ScreenPresenter::FindButton(const layout::LayoutNode& node,
                                                         float x, float y) const {
  for (const layout::LayoutNode& child : node.children) {
    if (const config::ComponentNode* hit = FindButton(child, x, y)) return hit;
  }
  if (node.source != nullptr && node.source->type() == L"button" &&
      node.bounds.contains({x, y})) {
    return node.source;
  }
  return nullptr;
}

const layout::LayoutNode* ScreenPresenter::FindNodeById(
    const layout::LayoutNode& node, std::wstring_view id) const {
  if (node.source != nullptr && node.source->id() == id) return &node;
  for (const layout::LayoutNode& child : node.children) {
    if (const layout::LayoutNode* found = FindNodeById(child, id)) return found;
  }
  return nullptr;
}

bool ScreenPresenter::InteractiveBounds(std::wstring_view id,
                                        render::Rect* out) const {
  if (out == nullptr || last_tree_ == nullptr) return false;
  const layout::LayoutNode* node = FindNodeById(*last_tree_, id);
  if (node == nullptr) return false;
  *out = {node->bounds.x, node->bounds.y + tab_strip_height_,
          node->bounds.width, node->bounds.height};
  return true;
}

bool ScreenPresenter::HandleMove(float x, float y) {
  bool changed = false;
  const bool in_strip = y >= caption_height_ && y < tab_strip_height_;
  const int tab = in_strip ? TabAt(x, y) : -1;
  if (tab != hover_tab_) {
    hover_tab_ = tab;
    changed = true;
  }
  const config::ComponentNode* hit =
      (!in_strip && last_tree_ != nullptr && y >= tab_strip_height_)
          ? FindButton(*last_tree_, x, y - tab_strip_height_)
          : nullptr;
  if (hit != hover_node_) {
    hover_node_ = hit;
    changed = true;
  }
  return changed;
}

bool ScreenPresenter::HandleDown(float x, float y) {
  bool changed = false;
  const bool in_strip = y >= caption_height_ && y < tab_strip_height_;
  const int tab = in_strip ? TabAt(x, y) : -1;
  if (tab != pressed_tab_) {
    pressed_tab_ = tab;
    changed = true;
  }
  const config::ComponentNode* hit =
      (!in_strip && last_tree_ != nullptr && y >= tab_strip_height_)
          ? FindButton(*last_tree_, x, y - tab_strip_height_)
          : nullptr;
  if (hit != pressed_node_) {
    pressed_node_ = hit;
    changed = true;
  }
  return changed;
}

bool ScreenPresenter::HandleClick(float x, float y) {
  if (y >= caption_height_ && y < tab_strip_height_) {
    const int tab = TabAt(x, y);
    pressed_tab_ = -1;
    if (tab >= 0 && tab_routes_[tab] != route_) {
      SwitchRoute(tab_routes_[tab]);
      return true;
    }
    return tab >= 0;
  }
  if (last_tree_ == nullptr) return false;
  const int suggestion = SuggestionAt(x, y - tab_strip_height_);
  if (suggestion >= 0 && suggestion_input_ != nullptr) {
    const json::Value* binding = suggestion_input_->Property(L"value_binding");
    if (binding != nullptr && binding->is_string()) {
      json::Value patch = json::Value::Object();
      patch.Set(binding->AsString(),
                json::Value::String(suggestion_values_[suggestion]));
      (void)ApplyStatePatch(route_, patch);
    }
    suggestion_input_ = nullptr;
    suggestion_rects_.clear();
    suggestion_values_.clear();
    return true;
  }
  suggestion_input_ = nullptr;
  suggestion_rects_.clear();
  suggestion_values_.clear();
  pressed_node_ = nullptr;
  return ClickNode(*last_tree_, x, y - tab_strip_height_);
}

void ScreenPresenter::PaintNode(const layout::LayoutNode& node) {
  const config::ComponentNode* source = node.source;
  if (source != nullptr) {
    if (!ResolvedBool(*source, L"visible", true)) return;
    const std::wstring& type = source->type();
    if (type == L"text") {
      render::TextAlign align = render::TextAlign::Left;
      const std::wstring requested = source->GetString(L"align", L"start");
      if (requested == L"center") align = render::TextAlign::Center;
      else if (requested == L"end") align = render::TextAlign::Right;
      backend_->DrawTextRun(ResolvedString(*source, L"text"), node.bounds,
                            StyleForText(*source),
                            Token(L"text", {255, 255, 255, 255}), align,
                            render::VerticalAlign::Top);
    } else if (type == L"button") {
      // Tab-state model: idle shows only a gray outline; hover brightens the
      // outline; press bumps; selected switches to the accent plus a tint.
      render::Rect box = node.bounds;
      const bool selected = ResolvedBool(*source, L"selected");
      render::Color outline = Token(L"borderStrong", {70, 76, 88, 255});
      const bool hovered = source == hover_node_;
      if (selected) {
        outline = Token(L"accent", {96, 165, 250, 255});
        render::Color tint = outline;
        tint.a = 90;
        backend_->FillRoundedRect(box, render::CornerRadius::Uniform(6.0f), tint);
      } else if (hovered) {
        outline = Token(L"accent", {96, 165, 250, 255});
      }
      if (source == pressed_node_) {
        outline = Shade(outline, -20);
        box.y += 1.0f;  // the bump
      }
      backend_->StrokeRoundedRect(box, render::CornerRadius::Uniform(6.0f), outline, 1.0f);
      if (hovered) {
        // Flush against the outer outline: no gap between the two strokes.
        const render::Rect inner{box.x + 1.0f, box.y + 1.0f, box.width - 2.0f,
                                 box.height - 2.0f};
        backend_->StrokeRoundedRect(inner, render::CornerRadius::Uniform(5.0f), outline,
                                    2.0f);
      }
      backend_->DrawTextRun(ResolvedString(*source, L"label"), box,
                            render::TextStyle{},
                            Token(L"text", {255, 255, 255, 255}), render::TextAlign::Center,
                            render::VerticalAlign::Middle);
    } else if (type == L"input" || type == L"combo") {
      backend_->StrokeRoundedRect(node.bounds, render::CornerRadius::Uniform(5.0f),
                                  Token(L"borderStrong", {70, 76, 88, 255}), 1.0f);
      const std::wstring binding_property =
          type == L"input" ? L"value_binding" : L"selected_value_binding";
      std::wstring value;
      if (const json::Value* binding = source->Property(binding_property);
          binding != nullptr && binding->is_string()) {
        if (const json::Value* resolved = ResolveBinding(route_, binding->AsString());
            resolved != nullptr && resolved->is_string()) {
          value = resolved->AsString();
        }
      }
      if (value.empty()) value = source->GetString(L"placeholder");
      if (type == L"combo" && !value.empty()) value += L"  \u25BE";
      backend_->DrawTextRun(value, node.bounds, render::TextStyle{},
                            Token(L"text", {255, 255, 255, 255}),
                            render::TextAlign::Left, render::VerticalAlign::Middle);
    } else if (type == L"toggle" || type == L"checkbox") {
      bool checked = false;
      if (const json::Value* binding = source->Property(L"checked_binding");
          binding != nullptr && binding->is_string()) {
        if (const json::Value* value = ResolveBinding(route_, binding->AsString());
            value != nullptr && value->is_bool()) {
          checked = value->AsBool();
        }
      }
      const render::Rect mark{node.bounds.x, node.bounds.y + 6.0f, 18.0f, 18.0f};
      backend_->StrokeRoundedRect(mark, render::CornerRadius::Uniform(5.0f),
                                  Token(L"borderStrong", {70, 76, 88, 255}), 1.0f);
      if (checked) {
        const render::Rect fill{mark.x + 3.0f, mark.y + 3.0f,
                                mark.width - 6.0f, mark.height - 6.0f};
        backend_->FillRoundedRect(fill, render::CornerRadius::Uniform(3.0f),
                                  Token(L"accent", {96, 165, 250, 255}));
      }
      const render::Rect label{node.bounds.x + 26.0f, node.bounds.y,
                               std::max(0.0f, node.bounds.width - 26.0f),
                               node.bounds.height};
      backend_->DrawTextRun(ResolvedString(*source, L"label"), label,
                            render::TextStyle{}, Token(L"text", {255, 255, 255, 255}),
                            render::TextAlign::Left, render::VerticalAlign::Middle);
    }
    if (source == focused_node_) {
      const render::Rect ring{node.bounds.x - 2.0f, node.bounds.y - 2.0f,
                              node.bounds.width + 4.0f, node.bounds.height + 4.0f};
      backend_->StrokeRoundedRect(ring, render::CornerRadius::Uniform(8.0f),
                                  Token(L"accent", {96, 165, 250, 255}), 2.0f);
    }
  }
  for (const layout::LayoutNode& child : node.children) {
    PaintNode(child);
  }
}

void ScreenPresenter::RebuildSuggestions(
    const config::ComponentNode* input, const render::Rect& bounds) {
  suggestion_input_ = input;
  suggestion_rects_.clear();
  suggestion_values_.clear();
  if (input == nullptr) return;
  const json::Value* source = input->Property(L"suggestions_binding");
  if (source == nullptr || !source->is_string()) return;
  const json::Value* values = ResolveBinding(route_, source->AsString());
  if (values == nullptr || !values->is_array()) return;

  std::wstring filter;
  if (const json::Value* binding = input->Property(L"value_binding");
      binding != nullptr && binding->is_string()) {
    if (const json::Value* current = ResolveBinding(route_, binding->AsString());
        current != nullptr && current->is_string()) {
      filter = current->AsString();
      std::transform(filter.begin(), filter.end(), filter.begin(),
                     [](wchar_t character) {
                       return static_cast<wchar_t>(std::towlower(character));
                     });
    }
  }
  for (const json::Value& value : values->items()) {
    if (!value.is_string()) continue;
    std::wstring candidate = value.AsString();
    std::wstring folded = candidate;
    std::transform(folded.begin(), folded.end(), folded.begin(),
                   [](wchar_t character) {
                     return static_cast<wchar_t>(std::towlower(character));
                   });
    if (!filter.empty() && folded.find(filter) == std::wstring::npos) continue;
    suggestion_values_.push_back(std::move(candidate));
    if (suggestion_values_.size() == 6) break;
  }
  for (std::size_t i = 0; i < suggestion_values_.size(); ++i) {
    suggestion_rects_.push_back(
        {bounds.x, bounds.bottom() + static_cast<float>(i) * 30.0f,
         bounds.width, 30.0f});
  }
}

int ScreenPresenter::SuggestionAt(float x, float y) const {
  for (std::size_t i = 0; i < suggestion_rects_.size(); ++i) {
    if (suggestion_rects_[i].contains({x, y})) return static_cast<int>(i);
  }
  return -1;
}

void ScreenPresenter::PaintSuggestions() {
  for (std::size_t i = 0; i < suggestion_rects_.size(); ++i) {
    backend_->FillRect(suggestion_rects_[i],
                       Token(L"surfaceAlt", {35, 39, 48, 255}));
    backend_->StrokeRect(suggestion_rects_[i],
                         Token(L"borderStrong", {70, 76, 88, 255}), 1.0f);
    backend_->DrawTextRun(suggestion_values_[i], suggestion_rects_[i],
                          render::TextStyle{},
                          Token(L"text", {255, 255, 255, 255}),
                          render::TextAlign::Left,
                          render::VerticalAlign::Middle);
  }
}

bool ScreenPresenter::HandleKey(int virtual_key) {
  if (virtual_key == VK_BACK) return EditFocusedInput(0, true);
  if (virtual_key != VK_TAB) return false;
  const bool backward = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  return !focus_.Advance(route_, backward).empty();
}

bool ScreenPresenter::HandleText(wchar_t character) {
  if (character < 0x20 || character == 0x7F) return false;
  return EditFocusedInput(character, false);
}

bool ScreenPresenter::EditFocusedInput(wchar_t character, bool backspace) {
  if (document_ == nullptr) return false;
  const config::ComponentNode* node =
      focus_.NodeFor(route_, focus_.Current(route_));
  if (node == nullptr || node->type() != L"input") return false;
  const json::Value* binding = node->Property(L"value_binding");
  if (binding == nullptr || !binding->is_string() || binding->AsString().empty()) {
    return false;
  }
  std::wstring value;
  if (const json::Value* current = ResolveBinding(route_, binding->AsString());
      current != nullptr && current->is_string()) {
    value = current->AsString();
  }
  if (backspace) {
    if (value.empty()) return true;
    value.pop_back();
  } else {
    const long long maximum = node->GetInt(L"maximum_length", 4096);
    if (static_cast<long long>(value.size()) >= maximum) return true;
    value.push_back(character);
  }
  json::Value patch = json::Value::Object();
  patch.Set(binding->AsString(), json::Value::String(std::move(value)));
  const bool applied = ApplyStatePatch(route_, patch).ok();
  if (applied && last_tree_ != nullptr) {
    const layout::LayoutNode* layout_node =
        FindNodeById(*last_tree_, node->id());
    if (layout_node != nullptr) RebuildSuggestions(node, layout_node->bounds);
  }
  return applied;
}

const json::Value* ScreenPresenter::ViewStateValue(
    std::wstring_view route, std::wstring_view binding) const {
  for (const auto& [state_route, state] : route_states_) {
    if (state_route != route) continue;
    const json::Value* value = &state;
    std::size_t begin = 0;
    while (begin < binding.size()) {
      const std::size_t dot = binding.find(L'.', begin);
      const std::wstring_view part = binding.substr(
          begin, dot == std::wstring_view::npos ? binding.size() - begin
                                                : dot - begin);
      value = value->Find(part);
      if (value == nullptr) return nullptr;
      if (dot == std::wstring_view::npos) return value;
      begin = dot + 1;
    }
  }
  return nullptr;
}

const json::Value* ScreenPresenter::ResolveBinding(
    std::wstring_view route, std::wstring_view binding) const {
  return ViewStateValue(route, binding);
}

json::Value ScreenPresenter::ResolveTemplate(const json::Value& value) const {
  std::wstring binding;
  if (IsBindingReference(value, &binding)) {
    const json::Value* resolved = ResolveBinding(route_, binding);
    return resolved != nullptr ? *resolved : json::Value::Null();
  }
  if (value.is_array()) {
    json::Value out = json::Value::Array();
    for (const json::Value& item : value.items()) out.Append(ResolveTemplate(item));
    return out;
  }
  if (value.is_object()) {
    json::Value out = json::Value::Object();
    for (const auto& [key, member] : value.members()) {
      out.Set(key, ResolveTemplate(member));
    }
    return out;
  }
  return value;
}

std::wstring ScreenPresenter::ResolvedString(
    const config::ComponentNode& node, std::wstring_view property,
    std::wstring_view fallback) const {
  const json::Value* value = node.Property(property);
  if (value == nullptr) return std::wstring(fallback);
  std::wstring binding;
  if (IsBindingReference(*value, &binding)) value = ResolveBinding(route_, binding);
  return value != nullptr && value->is_string() ? value->AsString()
                                                : std::wstring(fallback);
}

bool ScreenPresenter::ResolvedBool(const config::ComponentNode& node,
                                   std::wstring_view property,
                                   bool fallback) const {
  const json::Value* value = node.Property(property);
  if (value == nullptr) return fallback;
  std::wstring binding;
  if (IsBindingReference(*value, &binding)) value = ResolveBinding(route_, binding);
  return value != nullptr && value->is_bool() ? value->AsBool() : fallback;
}

void ScreenPresenter::InvalidateBoundNodes(
    const layout::LayoutNode& node,
    const std::vector<std::wstring>& changed) {
  if (node.source != nullptr) {
    bool affected = false;
    for (const auto& [key, value] : node.source->properties_) {
      if (ReferencesAny(value, changed) ||
          NamedBindingReferences(key, value, changed)) {
        affected = true;
        break;
      }
    }
    if (affected) {
      backend_->Invalidate({last_content_.x + node.bounds.x,
                            last_content_.y + tab_strip_height_ + node.bounds.y,
                            node.bounds.width, node.bounds.height});
    }
  }
  for (const layout::LayoutNode& child : node.children) {
    InvalidateBoundNodes(child, changed);
  }
}

core::Status ScreenPresenter::ApplyStatePatch(std::wstring_view route,
                                              const json::Value& patch) {
  if (!patch.is_object()) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"route state patch must be an object");
  }
  json::Value* state = nullptr;
  for (auto& [state_route, value] : route_states_) {
    if (state_route == route) {
      state = &value;
      break;
    }
  }
  if (state == nullptr) {
    route_states_.emplace_back(std::wstring(route), json::Value::Object());
    state = &route_states_.back().second;
  }
  std::vector<std::wstring> changed;
  for (const auto& [key, value] : patch.members()) {
    (void)value;
    changed.push_back(key);
  }
  MergeObject(state, patch);
  if (route == route_ && last_tree_ != nullptr) {
    InvalidateBoundNodes(*last_tree_, changed);
  }
  return core::Ok();
}

}  // namespace ui::presenter

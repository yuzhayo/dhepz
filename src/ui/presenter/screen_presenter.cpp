#include "ui/presenter/screen_presenter.h"

#include <windows.h>

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
}  // namespace

void ScreenPresenter::SetDocument(const config::ResolvedUiDocument* document) {
  document_ = document;
  focus_.SetDocument(document);
  route_.clear();
  if (document_ != nullptr) {
    route_ = document_->initial_route();
    focus_.EnterRoute(route_);
  }
}

void ScreenPresenter::SwitchRoute(std::wstring_view route) {
  if (document_ == nullptr || document_->FindRoute(route) == nullptr) return;
  route_ = std::wstring(route);
  focus_.EnterRoute(route_);
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
  tab_strip_height_ = tab_rects_.empty() ? 0.0f : caption_height_ + 36.0f;

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
  if (!(source->GetBool(L"tab_stop") && source->GetBool(L"visible", true) &&
        source->GetBool(L"enabled", true))) {
    return false;
  }
  const std::wstring id = focus_.IdFor(route_, source);
  if (id.empty()) return false;
  return focus_.SetFocus(route_, id);
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
  pressed_node_ = nullptr;
  return ClickNode(*last_tree_, x, y - tab_strip_height_);
}

void ScreenPresenter::PaintNode(const layout::LayoutNode& node) {
  const config::ComponentNode* source = node.source;
  if (source != nullptr) {
    const std::wstring& type = source->type();
    if (type == L"text") {
      backend_->DrawTextRun(source->GetString(L"text"), node.bounds, StyleForText(*source),
                            Token(L"text", {255, 255, 255, 255}), render::TextAlign::Left,
                            render::VerticalAlign::Top);
    } else if (type == L"button") {
      // Tab-state model: idle shows only a gray outline; hover brightens the
      // outline; press bumps; selected switches to the accent plus a tint.
      render::Rect box = node.bounds;
      const bool selected = source->GetBool(L"selected");
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
      backend_->DrawTextRun(source->GetString(L"label"), box, render::TextStyle{},
                            Token(L"text", {255, 255, 255, 255}), render::TextAlign::Center,
                            render::VerticalAlign::Middle);
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

bool ScreenPresenter::HandleKey(int virtual_key) {
  if (virtual_key != VK_TAB) return false;
  const bool backward = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  return !focus_.Advance(route_, backward).empty();
}

}  // namespace ui::presenter

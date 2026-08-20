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
  const render::Size size{content.width, content.height};
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
  PaintNode(*last_tree_);
  backend_->PopTranslation();
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
  const config::ComponentNode* hit =
      last_tree_ != nullptr ? FindButton(*last_tree_, x, y) : nullptr;
  if (hit == hover_node_) return false;
  hover_node_ = hit;
  return true;
}

bool ScreenPresenter::HandleDown(float x, float y) {
  const config::ComponentNode* hit =
      last_tree_ != nullptr ? FindButton(*last_tree_, x, y) : nullptr;
  const bool changed = hit != pressed_node_;
  pressed_node_ = hit;
  return changed;
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

void ScreenPresenter::PaintNode(const layout::LayoutNode& node) {
  const config::ComponentNode* source = node.source;
  if (source != nullptr) {
    const std::wstring& type = source->type();
    if (type == L"text") {
      backend_->DrawTextRun(source->GetString(L"text"), node.bounds, StyleForText(*source),
                            Token(L"text", {255, 255, 255, 255}), render::TextAlign::Left,
                            render::VerticalAlign::Top);
    } else if (type == L"button") {
      render::Rect box = node.bounds;
      render::Color fill = Token(L"surfaceAlt", {35, 39, 48, 255});
      if (source == pressed_node_) {
        fill = Shade(fill, -14);
        box.y += 1.0f;  // the bump
      } else if (source == hover_node_) {
        fill = Shade(fill, +14);
      }
      backend_->FillRoundedRect(box, render::CornerRadius::Uniform(6.0f), fill);
      backend_->StrokeRoundedRect(box, render::CornerRadius::Uniform(6.0f),
                                  Token(L"border", {48, 53, 64, 255}), 1.0f);
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

bool ScreenPresenter::HandleClick(float x, float y) {
  if (last_tree_ == nullptr) return false;
  pressed_node_ = nullptr;
  return ClickNode(*last_tree_, x, y);
}

}  // namespace ui::presenter

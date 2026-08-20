#include "ui/presenter/screen_presenter.h"

#include <windows.h>

#include "ui/layout/backdrop.h"

namespace ui::presenter {

ScreenPresenter::ScreenPresenter(render::RenderBackend* backend, std::wstring theme)
    : backend_(backend), engine_(backend), theme_(std::move(theme)) {}

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

void ScreenPresenter::Paint(const render::Rect& content) {
  if (document_ == nullptr || route_.empty()) return;
  const render::Size size{content.width, content.height};

  backend_->PushTranslation({content.x, content.y});
  layout::PaintBackdrop(backend_, &engine_, *document_, route_, size, theme_, nullptr);
  const layout::LayoutNode& tree = engine_.LayoutRoute(*document_, route_, size, nullptr);
  last_tree_ = &tree;
  PaintNode(tree);
  backend_->PopTranslation();
}

bool ScreenPresenter::ClickNode(const layout::LayoutNode& node, float x, float y) {
  for (const layout::LayoutNode& child : node.children) {
    if (ClickNode(child, x, y)) return true;
  }
  const config::ComponentNode* source = node.source;
  if (source == nullptr || source->id().empty()) return false;
  if (!node.bounds.contains({x, y})) return false;
  if (!(source->GetBool(L"tab_stop") && source->GetBool(L"visible", true) &&
        source->GetBool(L"enabled", true))) {
    return false;
  }
  return focus_.SetFocus(route_, source->id());
}

void ScreenPresenter::PaintNode(const layout::LayoutNode& node) {
  const config::ComponentNode* source = node.source;
  if (source != nullptr) {
    const std::wstring& type = source->type();
    if (type == L"text") {
      render::TextStyle style;
      const std::wstring variant = source->GetString(L"variant");
      if (variant == L"title") {
        style.size_px = 20.0f;
        style.weight = render::FontWeight::Semibold;
      } else if (variant == L"caption") {
        style.size_px = 12.0f;
      } else if (variant == L"monospace") {
        style.family = L"Cascadia Mono";
      }
      backend_->DrawTextRun(source->GetString(L"text"), node.bounds, style,
                            Token(L"text", {255, 255, 255, 255}), render::TextAlign::Left,
                            render::VerticalAlign::Top);
    } else if (type == L"button") {
      backend_->FillRoundedRect(node.bounds, render::CornerRadius::Uniform(6.0f),
                                Token(L"surfaceAlt", {35, 39, 48, 255}));
      backend_->StrokeRoundedRect(node.bounds, render::CornerRadius::Uniform(6.0f),
                                  Token(L"border", {48, 53, 64, 255}), 1.0f);
      backend_->DrawTextRun(source->GetString(L"label"), node.bounds, render::TextStyle{},
                            Token(L"text", {255, 255, 255, 255}), render::TextAlign::Center,
                            render::VerticalAlign::Middle);
    }
    if (!source->id().empty() && source->id() == focus_.Current(route_)) {
      const render::Rect ring{node.bounds.x - 2.0f, node.bounds.y - 2.0f,
                              node.bounds.width + 4.0f, node.bounds.height + 4.0f};
      backend_->StrokeRect(ring, Token(L"accent", {96, 165, 250, 255}), 2.0f);
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
  return ClickNode(*last_tree_, x, y);
}

}  // namespace ui::presenter

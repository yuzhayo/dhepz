#include "parent/ui/runtime/layout_engine.h"

#include <algorithm>

#include "ui/components/component_helpers.h"

namespace ui::layout {
namespace {

struct Padding {
  float left = 0.0f;
  float top = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;
};

Padding ReadPadding(const config::ComponentNode& node) {
  Padding padding;
  const json::Value* value = node.Find(L"padding");
  if (value != nullptr && value->is_object()) {
    padding.left = static_cast<float>(value->NumberField(L"left"));
    padding.top = static_cast<float>(value->NumberField(L"top"));
    padding.right = static_cast<float>(value->NumberField(L"right"));
    padding.bottom = static_cast<float>(value->NumberField(L"bottom"));
  }
  return padding;
}

bool IsVisible(const config::ComponentNode& node,
               const application::UiState& state) {
  return components::BoundBool(node, L"visible_binding", state,
                               node.GetBool(L"visible", true));
}

render::Rect ContentBounds(const config::ComponentNode& node, const render::Rect& bounds) {
  render::Rect base = node.type() == L"dialog"
                          ? components::DialogPanelBounds(node, bounds)
                          : bounds;
  const Padding padding = ReadPadding(node);
  base.x += padding.left;
  base.y += padding.top;
  base.width = std::max(0.0f, base.width - padding.left - padding.right);
  base.height = std::max(0.0f, base.height - padding.top - padding.bottom);
  if (node.type() == L"dialog" && !node.GetString(L"title").empty()) {
    base.y += 48.0f;
    base.height = std::max(0.0f, base.height - 48.0f);
  }
  return base;
}

void Offset(LayoutNode* node, float x, float y) {
  if (node == nullptr) return;
  node->bounds.x += x;
  node->bounds.y += y;
  for (LayoutNode& child : node->children) Offset(&child, x, y);
}

}  // namespace

LayoutEngine::LayoutEngine(render::RenderBackend* backend,
                           const components::ComponentRegistry* registry,
                           const application::UiState* state)
    : backend_(backend), registry_(registry), state_(state) {}

render::Size LayoutEngine::Measure(const config::ComponentNode& node, float max_width) {
  const auto cached = std::find_if(measurement_cache_.begin(), measurement_cache_.end(),
                                   [&node, max_width](const auto& entry) {
                                     return entry.first == &node && entry.second.first == max_width;
                                   });
  if (cached != measurement_cache_.end()) return cached->second.second;
  const components::ComponentDescriptor* descriptor = registry_->Find(node.type());
  const render::Size measured = descriptor != nullptr && descriptor->measure != nullptr
                                    ? descriptor->measure(node, *backend_, *state_, max_width)
                                    : render::Size{max_width, 0.0f};
  measurement_cache_.push_back({&node, {max_width, measured}});
  return measured;
}

LayoutNode LayoutEngine::BuildGrid(const config::ComponentNode& node, render::Rect bounds,
                                   const render::Rect& content, float gap) {
  LayoutNode out{&node, bounds, {}, node.type() == L"dialog" ||
                                               node.GetString(L"overflow") != L"visible"};
  std::vector<std::size_t> regular;
  for (std::size_t index = 0; index < node.children().size(); ++index) {
    if (node.children()[index].type() != L"dialog" &&
        IsVisible(node.children()[index], *state_)) regular.push_back(index);
  }
  const std::size_t count = regular.size();
  if (count == 0) {
    for (const config::ComponentNode& child : node.children()) {
      if (child.type() == L"dialog") out.children.push_back(Build(child, bounds));
    }
    return out;
  }
  const std::size_t columns = static_cast<std::size_t>(
      std::clamp(node.GetInt(L"columns", 2), 1LL, static_cast<long long>(count)));
  const std::size_t rows = (count + columns - 1) / columns;
  const float cell_width =
      std::max(0.0f, (content.width - gap * static_cast<float>(columns - 1)) /
                         static_cast<float>(columns));
  const float cell_height =
      std::max(0.0f, (content.height - gap * static_cast<float>(rows - 1)) /
                         static_cast<float>(rows));
  out.children.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    const std::size_t column = index % columns;
    const std::size_t row = index / columns;
    const render::Rect slot{content.x + column * (cell_width + gap),
                            content.y + row * (cell_height + gap), cell_width, cell_height};
    out.children.push_back(Build(node.children()[regular[index]], slot));
  }
  for (const config::ComponentNode& child : node.children()) {
    if (child.type() == L"dialog") out.children.push_back(Build(child, bounds));
  }
  return out;
}

LayoutNode LayoutEngine::BuildFlow(const config::ComponentNode& node, render::Rect bounds,
                                   const render::Rect& content, float gap) {
  LayoutNode out{&node, bounds, {}, node.type() == L"dialog" ||
                                               node.GetString(L"overflow") != L"visible"};
  float x = content.x;
  float y = content.y;
  float line_height = 0.0f;
  for (const config::ComponentNode& child : node.children()) {
    if (child.type() == L"dialog" || !IsVisible(child, *state_)) continue;
    render::Size desired = Measure(child, content.width);
    if (desired.width <= 0.0f) desired.width = content.width;
    if (desired.height <= 0.0f) desired.height = 34.0f;
    desired.width = std::min(desired.width, content.width);
    if (x > content.x && x + desired.width > content.right()) {
      x = content.x;
      y += line_height + gap;
      line_height = 0.0f;
    }
    out.children.push_back(Build(child, {x, y, desired.width, desired.height}));
    x += desired.width + gap;
    line_height = std::max(line_height, desired.height);
  }
  for (const config::ComponentNode& child : node.children()) {
    if (child.type() == L"dialog") out.children.push_back(Build(child, bounds));
  }
  return out;
}

LayoutNode LayoutEngine::BuildContainer(const config::ComponentNode& node, render::Rect bounds) {
  const render::Rect content = ContentBounds(node, bounds);
  const float gap = static_cast<float>(node.GetInt(L"gap", 8));
  std::wstring direction = node.GetString(L"direction", L"column");
  if (node.type() == L"tabs") direction = L"row";
  if (direction == L"grid") return BuildGrid(node, bounds, content, gap);
  if (direction == L"flow" || (direction == L"row" && node.GetBool(L"wrap"))) {
    return BuildFlow(node, bounds, content, gap);
  }

  LayoutNode out{&node, bounds, {}, node.type() == L"dialog" ||
                                               node.GetString(L"overflow") != L"visible"};
  const bool row = direction == L"row";
  std::vector<std::size_t> regular;
  std::vector<render::Size> desired;
  desired.reserve(node.children().size());
  float fixed = 0.0f;
  std::size_t flexible = 0;
  for (std::size_t index = 0; index < node.children().size(); ++index) {
    const config::ComponentNode& child = node.children()[index];
    if (child.type() == L"dialog" || !IsVisible(child, *state_)) continue;
    regular.push_back(index);
    render::Size size = Measure(child, content.width);
    const float main = row ? size.width : size.height;
    if (main <= 0.0f) {
      ++flexible;
    } else {
      fixed += main;
    }
    desired.push_back(size);
  }
  if (desired.size() > 1) fixed += gap * static_cast<float>(desired.size() - 1);
  const float extent = row ? content.width : content.height;
  const float flexible_size = flexible == 0
                                  ? 0.0f
                                  : std::max(0.0f, extent - fixed) /
                                        static_cast<float>(flexible);
  const float used = fixed + flexible_size * static_cast<float>(flexible);
  const float remaining = std::max(0.0f, extent - used);
  const std::wstring justify = node.GetString(L"justify", L"start");
  float cursor = row ? content.x : content.y;
  float distributed = 0.0f;
  if (justify == L"center") cursor += remaining / 2.0f;
  if (justify == L"end") cursor += remaining;
  if (justify == L"space-between" && desired.size() > 1) {
    distributed = remaining / static_cast<float>(desired.size() - 1);
  }
  const std::wstring align = node.GetString(L"align", L"stretch");
  for (std::size_t index = 0; index < regular.size(); ++index) {
    float main = row ? desired[index].width : desired[index].height;
    if (main <= 0.0f) main = flexible_size;
    float cross = row ? desired[index].height : desired[index].width;
    const float cross_extent = row ? content.height : content.width;
    if (cross <= 0.0f || align == L"stretch") cross = cross_extent;
    cross = std::min(cross, cross_extent);
    render::Rect slot = row ? render::Rect{cursor, content.y, main, cross}
                            : render::Rect{content.x, cursor, cross, main};
    const float extra = std::max(0.0f, cross_extent - cross);
    if (row) {
      if (align == L"center") slot.y += extra / 2.0f;
      if (align == L"end") slot.y += extra;
    } else {
      if (align == L"center") slot.x += extra / 2.0f;
      if (align == L"end") slot.x += extra;
    }
    out.children.push_back(Build(node.children()[regular[index]], slot));
    cursor += main + gap + distributed;
  }

  if (node.GetString(L"overflow") == L"scroll") {
    const float offset = static_cast<float>(
        std::max(0LL, state_->Integer(node.GetString(L"scroll_binding"))));
    for (LayoutNode& child : out.children) Offset(&child, row ? -offset : 0.0f,
                                                   row ? 0.0f : -offset);
  }
  for (const config::ComponentNode& child : node.children()) {
    if (child.type() == L"dialog") out.children.push_back(Build(child, bounds));
  }
  return out;
}

LayoutNode LayoutEngine::Build(const config::ComponentNode& node,
                               const render::Rect& available) {
  if (!IsVisible(node, *state_)) {
    return {&node, {}, {}, false};
  }
  if (node.type() == L"dialog" &&
      !components::BoundBool(node, L"open_binding", *state_)) {
    return {&node, {}, {}, false};
  }
  render::Rect bounds = available;
  if (node.type() != L"dialog") {
    const long long width = node.GetInt(L"width");
    const long long height = node.GetInt(L"height");
    if (width > 0) bounds.width = std::min(bounds.width, static_cast<float>(width));
    if (height > 0) bounds.height = std::min(bounds.height, static_cast<float>(height));
  }
  const components::ComponentDescriptor* descriptor = registry_->Find(node.type());
  if (descriptor != nullptr && descriptor->container && !node.children().empty()) {
    return BuildContainer(node, bounds);
  }
  const render::Size measured = Measure(node, bounds.width);
  if (measured.width > 0.0f) bounds.width = std::min(bounds.width, measured.width);
  if (measured.height > 0.0f) bounds.height = std::min(bounds.height, measured.height);
  return {&node, bounds, {}, false};
}

const LayoutNode& LayoutEngine::LayoutRoute(const config::ResolvedUiDocument& document,
                                            std::wstring_view route, render::Size size) {
  const std::uint64_t revision = state_ != nullptr ? state_->revision() : 0;
  if (cache_valid_ && cache_document_ == &document && cache_route_ == route &&
      cache_size_.width == size.width && cache_size_.height == size.height &&
      cache_revision_ == revision) {
    return tree_;
  }
  if (cache_document_ != &document || cache_revision_ != revision) {
    measurement_cache_.clear();
  }
  tree_ = {};
  const config::Route* found = document.FindRoute(route);
  if (found != nullptr) tree_ = Build(found->root, {0.0f, 0.0f, size.width, size.height});
  cache_document_ = &document;
  cache_route_ = std::wstring(route);
  cache_size_ = size;
  cache_revision_ = revision;
  cache_valid_ = true;
  return tree_;
}

}  // namespace ui::layout

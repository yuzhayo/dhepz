#include "ui/layout/layout_engine.h"

#include <algorithm>
#include <cmath>

namespace ui::layout {
namespace {

const json::Value* Property(const config::ComponentNode& node, std::wstring_view name) {
  for (const auto& [key, value] : node.properties_) {
    if (key == name) return &value;
  }
  return nullptr;
}

struct Padding {
  float left = 0.0f;
  float top = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;
};

Padding ReadPadding(const config::ComponentNode& node) {
  Padding padding;
  const json::Value* value = Property(node, L"padding");
  if (value != nullptr && value->is_object()) {
    padding.left = static_cast<float>(value->NumberField(L"left"));
    padding.top = static_cast<float>(value->NumberField(L"top"));
    padding.right = static_cast<float>(value->NumberField(L"right"));
    padding.bottom = static_cast<float>(value->NumberField(L"bottom"));
  }
  return padding;
}

bool IsContainerLike(const std::wstring& type) {
  return type == L"container" || type == L"screen" || type == L"window" || type == L"dialog" ||
         type == L"card" || type == L"tabs";
}

}  // namespace

render::Size LayoutEngine::MeasureText(const config::ComponentNode& node,
                                       std::wstring_view text,
                                       const render::TextStyle* style) {
  for (const auto& [memo_node, size] : memo_) {
    if (memo_node == &node) return size;
  }
  ++measure_calls_;
  const render::TextStyle fallback{};
  const render::Size size =
      backend_->MeasureText(text, style != nullptr ? *style : fallback, 0.0f);
  memo_.emplace_back(&node, size);
  return size;
}

void LayoutEngine::set_text_style_provider(
    std::function<render::TextStyle(const config::ComponentNode&)> provider) {
  text_style_provider_ = std::move(provider);
}

LayoutNode LayoutEngine::Build(const config::ComponentNode& node, const render::Rect& available,
                               const ListModel* model) {
  LayoutNode out;
  out.source = &node;
  out.bounds = available;
  const std::wstring& type = node.type();

  if (type == L"text") {
    const render::TextStyle style =
        text_style_provider_ ? text_style_provider_(node) : render::TextStyle{};
    const json::Value* text_value = Property(node, L"text");
    const bool dynamic_text = text_value != nullptr && !text_value->is_string();
    const render::Size size = MeasureText(
        node, dynamic_text ? std::wstring_view(L"M")
                           : std::wstring_view(node.GetString(L"text")),
        &style);
    out.bounds.width = dynamic_text ? available.width
                                    : std::min(size.width, available.width);
    out.bounds.height = size.height;
    return out;
  }

  if (type == L"button") {
    const json::Value* label = Property(node, L"label");
    const render::Size size = MeasureText(
        node, label != nullptr && !label->is_string()
                  ? std::wstring_view(L"Button")
                  : std::wstring_view(node.GetString(L"label")));
    out.bounds.width = std::min(size.width + 16.0f, available.width);
    out.bounds.height = std::max(size.height + 8.0f, 24.0f);
    return out;
  }

  if (type == L"input" || type == L"combo" || type == L"checkbox" ||
      type == L"toggle") {
    const std::wstring text = node.GetString(
        L"label", node.GetString(L"placeholder", type));
    const render::Size size = MeasureText(node, text);
    out.bounds.width = std::min(std::max(size.width + 24.0f, 160.0f),
                                available.width);
    out.bounds.height = std::max(size.height + 10.0f, 30.0f);
    return out;
  }

  if (type == L"list") {
    out.row_height = static_cast<float>(node.GetInt(L"row_height", 32));
    const int overscan = static_cast<int>(node.GetInt(L"overscan_rows", 2));
    out.row_count = model != nullptr ? model->count : 0;
    out.scroll_offset = scroll_offset_;
    const float viewport = available.height;
    int first = static_cast<int>(std::floor(out.scroll_offset / out.row_height)) - overscan;
    first = std::clamp(first, 0, std::max(0, out.row_count - 1));
    int count = static_cast<int>(std::ceil(viewport / out.row_height)) + 2 * overscan;
    count = std::min(count, out.row_count - first);
    out.first_visible_row = first;
    out.visible_row_count = std::max(0, count);

    // Rows materialize from the template's text when it is literal; binding
    // text renders a placeholder until data bindings land with the modules.
    const json::Value* template_value = Property(node, L"item_template");
    std::wstring template_text;
    if (template_value != nullptr && template_value->is_object()) {
      const json::Value* text = template_value->Find(L"text");
      if (text != nullptr && text->is_string()) {
        template_text = text->AsString();
      }
    }
    for (int row = first; row < first + out.visible_row_count; ++row) {
      LayoutNode row_node;
      row_node.source = &node;  // memo key: one measurement per template
      row_node.bounds = {available.x,
                         available.y + row * out.row_height - out.scroll_offset,
                         available.width, out.row_height};
      const render::Size size =
          MeasureText(node, template_text.empty() ? L"row" : template_text);
      (void)size;
      out.children.push_back(std::move(row_node));
    }
    out.bounds.height = std::min(viewport, out.row_count * out.row_height);
    return out;
  }

  if (IsContainerLike(type)) {
    const Padding padding = ReadPadding(node);
    const float gap = static_cast<float>(node.GetInt(L"gap", 8));
    const std::wstring direction = node.GetString(L"direction", L"column");
    const bool row = direction == L"row";
    // grid/flow land with the first screen that needs them; column until then.
    render::Rect content{available.x + padding.left, available.y + padding.top,
                         available.width - padding.left - padding.right,
                         available.height - padding.top - padding.bottom};
    float cursor = row ? content.x : content.y;
    for (const config::ComponentNode& child : node.children()) {
      render::Rect child_available = content;
      if (row) {
        child_available.x = cursor;
        child_available.width = std::max(0.0f, content.right() - cursor);
      } else {
        child_available.y = cursor;
        child_available.height = std::max(0.0f, content.bottom() - cursor);
      }
      LayoutNode child_node = Build(child, child_available, model);
      cursor += (row ? child_node.bounds.width : child_node.bounds.height) + gap;
      out.children.push_back(std::move(child_node));
    }
    return out;
  }

  // Leaf components without intrinsic metrics take their slot; the paint
  // layer decides their look.
  return out;
}

const LayoutNode& LayoutEngine::LayoutRoute(const config::ResolvedUiDocument& document,
                                            std::wstring_view route, render::Size size,
                                            const ListModel* model) {
  if (&document != memo_document_) {
    memo_.clear();
    memo_document_ = &document;
    cache_valid_ = false;
  }
  if (cache_valid_ && cache_route_ == route && cache_size_.width == size.width &&
      cache_size_.height == size.height) {
    return cache_;
  }
  const config::Route* found = document.FindRoute(route);
  cache_ = LayoutNode{};
  if (found != nullptr) {
    scroll_offset_ = 0.0f;
    cache_ = Build(found->root, {0.0f, 0.0f, size.width, size.height}, model);
  }
  cache_route_ = std::wstring(route);
  cache_size_ = size;
  cache_valid_ = true;
  return cache_;
}

const LayoutNode& LayoutEngine::LayoutScrolled(const config::ResolvedUiDocument& document,
                                               std::wstring_view route, render::Size size,
                                               std::wstring_view list_id, float scroll_offset,
                                               const ListModel* model) {
  (void)list_id;  // one list per route in this slice; the id arrives with screens
  scroll_offset_ = scroll_offset;
  cache_valid_ = false;
  return LayoutRoute(document, route, size, model);
}

}  // namespace ui::layout

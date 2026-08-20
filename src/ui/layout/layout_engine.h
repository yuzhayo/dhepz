// The layout pipeline (#56): ResolvedUiDocument -> measured rects, with the
// two performance properties Phase 2 is gated on.
//
//   - List virtualization: a list materializes only the rows intersecting
//     the visible window (plus overscan); everything else is a count and a
//     row height.
//   - Incremental layout: text measurement is memoized per source node, so
//     a scroll or a small change re-measures only what it touches, and a
//     warm route switch is a cache lookup.
//
// Measurement goes through the render seam's MeasureText, which is valid
// outside a paint scope by contract. grid/flow container directions land
// with the first screen that needs them (amended on the issue); they lay
// out as column until then.
#pragma once

#include <string>
#include <vector>

#include "render/render_backend.h"
#include "ui/config/resolved_ui_document.h"

namespace ui::layout {

struct LayoutNode {
  const config::ComponentNode* source = nullptr;
  render::Rect bounds;  // logical px, relative to the route origin
  std::vector<LayoutNode> children;

  // Lists only.
  int row_count = 0;
  float row_height = 0.0f;
  float scroll_offset = 0.0f;
  int first_visible_row = 0;
  int visible_row_count = 0;
};

// Binding data arrives with modules; the pipeline only needs the row count.
struct ListModel {
  int count = 0;
};

class LayoutEngine final {
 public:
  explicit LayoutEngine(render::RenderBackend* backend) : backend_(backend) {}

  // Full layout of a route at `size`. Results are cached per (document,
  // route, size): a warm switch returns the cached tree without measuring.
  const LayoutNode& LayoutRoute(const config::ResolvedUiDocument& document,
                                std::wstring_view route, render::Size size,
                                const ListModel* model);

  // Scroll re-layout for one list: the memoized measurements of everything
  // outside the list are reused; only newly visible rows measure.
  const LayoutNode& LayoutScrolled(const config::ResolvedUiDocument& document,
                                   std::wstring_view route, render::Size size,
                                   std::wstring_view list_id, float scroll_offset,
                                   const ListModel* model);

  // Test seam: how many times the seam's MeasureText ran.
  int measure_calls() const { return measure_calls_; }

 private:
  LayoutNode Build(const config::ComponentNode& node, const render::Rect& available,
                   const ListModel* model);
  render::Size MeasureText(const config::ComponentNode& node, std::wstring_view text);

  render::RenderBackend* backend_;
  int measure_calls_ = 0;
  float scroll_offset_ = 0.0f;

  const void* memo_document_ = nullptr;
  std::vector<std::pair<const config::ComponentNode*, render::Size>> memo_;

  std::wstring cache_route_;
  render::Size cache_size_{};
  LayoutNode cache_;
  bool cache_valid_ = false;
};

}  // namespace ui::layout

#pragma once

#include <limits>
#include <string>
#include <vector>

#include "ui/components/component_registry.h"
#include "parent/ui/runtime/layout_engine.h"

namespace ui::focus {

class FocusCoordinator final {
 public:
  explicit FocusCoordinator(const components::ComponentRegistry* registry);

  void Clear();
  void Blur();
  void Rebuild(const layout::LayoutNode* tree);
  const config::ComponentNode* current() const;
  bool Focus(const config::ComponentNode* node);
  bool Advance(bool backward);

 private:
  void Collect(const layout::LayoutNode& node);

  const components::ComponentRegistry* registry_;
  std::vector<const config::ComponentNode*> order_;
  static constexpr std::size_t kNoFocus = (std::numeric_limits<std::size_t>::max)();
  std::size_t index_ = kNoFocus;
};

}  // namespace ui::focus

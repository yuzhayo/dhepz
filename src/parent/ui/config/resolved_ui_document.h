#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/json.h"
#include "core/status.h"
#include "parent/ui/config/ui_schema.h"

namespace ui::config {

struct ScreenSource {
  std::wstring name;
  std::wstring text;
};

class ComponentNode {
 public:
  ComponentNode() = default;
  ComponentNode(std::wstring type, std::wstring id)
      : type_(std::move(type)), id_(std::move(id)) {}

  const std::wstring& type() const { return type_; }
  const std::wstring& id() const { return id_; }
  std::wstring GetString(std::wstring_view name, std::wstring_view fallback = {}) const;
  long long GetInt(std::wstring_view name, long long fallback = 0) const;
  bool GetBool(std::wstring_view name, bool fallback = false) const;
  const json::Value* Find(std::wstring_view name) const;
  const std::vector<ComponentNode>& children() const { return children_; }

  void SetProperty(std::wstring name, json::Value value);
  void AppendChild(ComponentNode child);

 private:
  std::wstring type_;
  std::wstring id_;
  std::vector<std::pair<std::wstring, json::Value>> properties_;
  std::vector<ComponentNode> children_;
};

struct Route {
  std::wstring id;
  ComponentNode root;
};

class ResolvedUiDocument final {
 public:
  const Route* FindRoute(std::wstring_view route) const;
  const std::wstring& initial_route() const { return initial_route_; }

 private:
  friend core::Status ResolveDocument(const json::Value&, const std::vector<ScreenSource>&,
                                      std::vector<Diagnostic>*,
                                      std::unique_ptr<ResolvedUiDocument>*);
  std::vector<Route> routes_;
  std::wstring initial_route_;
};

core::Status ResolveDocument(const json::Value& core, const std::vector<ScreenSource>& sources,
                             std::vector<Diagnostic>* diagnostics,
                             std::unique_ptr<ResolvedUiDocument>* document);

}  // namespace ui::config

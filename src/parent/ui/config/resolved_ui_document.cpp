#include "parent/ui/config/resolved_ui_document.h"

#include <algorithm>
#include <functional>

namespace ui::config {
namespace {

const json::Value* CatalogProperties(const json::Value& core, std::wstring_view type) {
  const json::Value* components = core.ObjectField(L"components");
  const json::Value* catalog = components != nullptr ? components->Find(type) : nullptr;
  return catalog != nullptr ? catalog->ObjectField(L"properties") : nullptr;
}

void ApplyDefaults(ComponentNode* node, const json::Value* definitions) {
  if (node == nullptr || definitions == nullptr) return;
  for (const auto& [name, definition] : definitions->members()) {
    const json::Value* value = definition.Find(L"default");
    if (value != nullptr) node->SetProperty(name, *value);
  }
}

ComponentNode BuildNode(const json::Value& core, const json::Value& source) {
  ComponentNode node(source.StringField(L"type"), source.StringField(L"id"));
  ApplyDefaults(&node, CatalogProperties(core, node.type()));
  const json::Value* common = core.ObjectField(L"common");
  ApplyDefaults(&node, common != nullptr ? common->ObjectField(L"properties") : nullptr);
  for (const auto& [name, value] : source.members()) {
    if (name == L"type" || name == L"children") continue;
    node.SetProperty(name, value);
  }
  const json::Value* children = source.ArrayField(L"children");
  if (children != nullptr) {
    for (const json::Value& child : children->items()) {
      node.AppendChild(BuildNode(core, child));
    }
  }
  return node;
}

}  // namespace

const json::Value* ComponentNode::Find(std::wstring_view name) const {
  const auto found = std::find_if(properties_.begin(), properties_.end(), [name](const auto& item) {
    return item.first == name;
  });
  return found == properties_.end() ? nullptr : &found->second;
}

void ComponentNode::SetProperty(std::wstring name, json::Value value) {
  const auto found = std::find_if(properties_.begin(), properties_.end(),
                                  [&name](const auto& item) { return item.first == name; });
  if (found == properties_.end()) {
    properties_.emplace_back(std::move(name), std::move(value));
  } else {
    found->second = std::move(value);
  }
}

void ComponentNode::AppendChild(ComponentNode child) {
  children_.push_back(std::move(child));
}

std::wstring ComponentNode::GetString(std::wstring_view name, std::wstring_view fallback) const {
  const json::Value* value = Find(name);
  return value != nullptr && value->is_string() ? value->AsString() : std::wstring(fallback);
}

long long ComponentNode::GetInt(std::wstring_view name, long long fallback) const {
  const json::Value* value = Find(name);
  return value != nullptr && value->is_number()
             ? static_cast<long long>(value->AsNumber())
             : fallback;
}

bool ComponentNode::GetBool(std::wstring_view name, bool fallback) const {
  const json::Value* value = Find(name);
  return value != nullptr && value->is_bool() ? value->AsBool() : fallback;
}

const Route* ResolvedUiDocument::FindRoute(std::wstring_view route) const {
  const auto found = std::find_if(routes_.begin(), routes_.end(), [route](const Route& item) {
    return item.id == route;
  });
  return found == routes_.end() ? nullptr : &*found;
}

core::Status ResolveDocument(const json::Value& core, const std::vector<ScreenSource>& sources,
                             std::vector<Diagnostic>* diagnostics,
                             std::unique_ptr<ResolvedUiDocument>* document) {
  if (diagnostics == nullptr || document == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument, L"UI resolver outputs are required");
  }
  diagnostics->clear();
  document->reset();
  DHEPZ_RETURN_IF_ERROR(ValidateCore(core, diagnostics));
  auto resolved = std::make_unique<ResolvedUiDocument>();
  std::wstring requested_initial_route;
  for (const ScreenSource& source : sources) {
    json::Value parsed;
    const core::Status parsed_status = json::Parse(source.text, &parsed);
    if (!parsed_status.ok()) {
      diagnostics->push_back({source.name + L": " + parsed_status.Message(), 0, 0});
      return parsed_status;
    }
    DHEPZ_RETURN_IF_ERROR(ValidateScreen(core, parsed, diagnostics));
    const json::Value* components = parsed.ArrayField(L"components");
    std::function<core::Status(const json::Value&)> collect;
    collect = [&](const json::Value& item) -> core::Status {
      const std::wstring type = item.StringField(L"type");
      if (type == L"window" && requested_initial_route.empty()) {
        requested_initial_route = item.StringField(L"initial_route");
      }
      if (type == L"screen") {
        const std::wstring route_id = item.StringField(L"route_id");
        if (resolved->FindRoute(route_id) != nullptr) {
          return DHEPZ_ERR(core::ErrorCode::AlreadyExists,
                           L"duplicate UI route '" + route_id + L"'");
        }
        resolved->routes_.push_back({route_id, BuildNode(core, item)});
        return core::Ok();
      }
      const json::Value* children = item.ArrayField(L"children");
      if (children != nullptr) {
        for (const json::Value& child : children->items()) {
          DHEPZ_RETURN_IF_ERROR(collect(child));
        }
      }
      return core::Ok();
    };
    for (const json::Value& item : components->items()) {
      DHEPZ_RETURN_IF_ERROR(collect(item));
    }
  }
  if (resolved->routes_.empty()) {
    return DHEPZ_ERR(core::ErrorCode::NotFound, L"required UI route is missing");
  }
  if (!requested_initial_route.empty()) {
    if (resolved->FindRoute(requested_initial_route) == nullptr) {
      return DHEPZ_ERR(core::ErrorCode::NotFound,
                       L"initial UI route '" + requested_initial_route + L"' is missing");
    }
    resolved->initial_route_ = std::move(requested_initial_route);
  } else {
    resolved->initial_route_ = resolved->routes_.front().id;
  }
  *document = std::move(resolved);
  return core::Ok();
}

}  // namespace ui::config

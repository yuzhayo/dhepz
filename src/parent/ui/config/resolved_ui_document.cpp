#include "parent/ui/config/resolved_ui_document.h"

#include <algorithm>
#include <functional>

namespace ui::config {
namespace {

bool ParseHexColor(const std::wstring& text, Rgba* out) {
  if (out == nullptr || (text.size() != 7 && text.size() != 9) || text[0] != L'#') return false;
  auto nibble = [](wchar_t value, unsigned int* out_value) {
    if (value >= L'0' && value <= L'9') *out_value = static_cast<unsigned int>(value - L'0');
    else if (value >= L'a' && value <= L'f') *out_value = static_cast<unsigned int>(value - L'a' + 10);
    else if (value >= L'A' && value <= L'F') *out_value = static_cast<unsigned int>(value - L'A' + 10);
    else return false;
    return true;
  };
  unsigned int channels[4]{0, 0, 0, 255};
  const int count = text.size() == 9 ? 4 : 3;
  for (int channel = 0; channel < count; ++channel) {
    unsigned int high = 0;
    unsigned int low = 0;
    if (!nibble(text[1 + channel * 2], &high) || !nibble(text[2 + channel * 2], &low)) {
      return false;
    }
    channels[channel] = high * 16 + low;
  }
  *out = {static_cast<unsigned char>(channels[0]), static_cast<unsigned char>(channels[1]),
          static_cast<unsigned char>(channels[2]), static_cast<unsigned char>(channels[3])};
  return true;
}

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

bool ResolvedUiDocument::Token(std::wstring_view theme, std::wstring_view name,
                               Rgba* color) const {
  for (std::size_t index = 0; index < theme_names_.size(); ++index) {
    if (theme_names_[index] != theme) continue;
    for (const auto& [token, value] : theme_tokens_[index]) {
      if (token == name) {
        if (color != nullptr) *color = value;
        return true;
      }
    }
  }
  return false;
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
  const json::Value* tokens = core.ObjectField(L"tokens");
  if (tokens != nullptr) {
    for (const auto& [theme_name, theme] : tokens->members()) {
      std::vector<std::pair<std::wstring, Rgba>> colors;
      if (theme.is_object()) {
        for (const auto& [token_name, encoded] : theme.members()) {
          Rgba color;
          if (encoded.is_string() && ParseHexColor(encoded.AsString(), &color)) {
            colors.emplace_back(token_name, color);
          }
        }
      }
      resolved->theme_names_.push_back(theme_name);
      resolved->theme_tokens_.push_back(std::move(colors));
    }
  }
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
        Route route;
        route.id = route_id;
        route.root = BuildNode(core, item);
        const std::wstring backdrop = item.StringField(L"backdrop");
        if (!backdrop.empty()) {
          if (backdrop.rfind(L"screen:", 0) == 0) {
            route.backdrop_kind = Route::BackdropKind::Screen;
            route.backdrop_value = backdrop.substr(7);
          } else {
            Rgba ignored;
            route.backdrop_kind = resolved->Token(L"dark", backdrop, &ignored)
                                      ? Route::BackdropKind::Color
                                      : Route::BackdropKind::Image;
            route.backdrop_value = backdrop;
          }
        }
        resolved->routes_.push_back(std::move(route));
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
  for (const Route& route : resolved->routes_) {
    if (route.backdrop_kind == Route::BackdropKind::Screen &&
        (route.backdrop_value == route.id || resolved->FindRoute(route.backdrop_value) == nullptr)) {
      return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                       L"backdrop screen '" + route.backdrop_value + L"' is invalid");
    }
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

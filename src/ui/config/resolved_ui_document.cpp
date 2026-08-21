#include "ui/config/resolved_ui_document.h"

#include <algorithm>
#include <cwchar>

namespace ui::config {
namespace {

bool ParseHexColor(const std::wstring& text, Rgba* out) {
  if (text.size() != 7 && text.size() != 9) return false;
  if (text.front() != L'#') return false;
  auto nibble = [](wchar_t c, unsigned* value) {
    if (c >= L'0' && c <= L'9') { *value = static_cast<unsigned>(c - L'0'); return true; }
    if (c >= L'a' && c <= L'f') { *value = static_cast<unsigned>(c - L'a') + 10; return true; }
    if (c >= L'A' && c <= L'F') { *value = static_cast<unsigned>(c - L'A') + 10; return true; }
    return false;
  };
  unsigned values[4] = {0, 0, 0, 255};
  for (int channel = 0; channel < (text.size() == 9 ? 4 : 3); ++channel) {
    unsigned high = 0;
    unsigned low = 0;
    if (!nibble(text[1 + channel * 2], &high) || !nibble(text[2 + channel * 2], &low)) {
      return false;
    }
    values[channel] = high * 16 + low;
  }
  out->r = static_cast<unsigned char>(values[0]);
  out->g = static_cast<unsigned char>(values[1]);
  out->b = static_cast<unsigned char>(values[2]);
  out->a = static_cast<unsigned char>(values[3]);
  return true;
}

const json::Value* CatalogType(const json::Value& core, std::wstring_view type) {
  const json::Value* components = core.ObjectField(L"components");
  if (components == nullptr) return nullptr;
  return components->Find(type);
}

// Extracts "(line N, column M)" from a parser Status message so syntax
// errors carry the same structured position as semantic diagnostics.
void SplitParseMessage(const std::wstring& message, std::wstring* text, int* line,
                       int* column) {
  const std::size_t open = message.rfind(L" (line ");
  if (open == std::wstring::npos) {
    *text = message;
    return;
  }
  *text = message.substr(0, open);
  int parsed_line = 0;
  int parsed_column = 0;
  if (swscanf_s(message.c_str() + open, L" (line %d, column %d)", &parsed_line,
                &parsed_column) == 2) {
    *line = parsed_line;
    *column = parsed_column;
  }
}

ComponentNode BuildNode(const json::Value& core, const json::Value& component) {
  ComponentNode node;
  node.type_ = component.StringField(L"type");
  node.id_ = component.StringField(L"id");

  const json::Value* catalog = CatalogType(core, node.type_);
  const json::Value* common = core.ObjectField(L"common");
  for (const json::Value* properties :
       {catalog != nullptr ? catalog->ObjectField(L"properties") : nullptr,
        common != nullptr ? common->ObjectField(L"properties") : nullptr}) {
    if (properties == nullptr) continue;
    for (const auto& [name, definition] : properties->members()) {
      if (!definition.is_object()) continue;
      const json::Value* default_value = definition.Find(L"default");
      if (default_value != nullptr) {
        node.properties_.emplace_back(name, *default_value);
      }
    }
  }
  for (const auto& [name, value] : component.members()) {
    if (name == L"type" || name == L"children") continue;
    const auto existing = std::find_if(
        node.properties_.begin(), node.properties_.end(),
        [&name](const auto& pair) { return pair.first == name; });
    if (existing != node.properties_.end()) {
      existing->second = value;
    } else {
      node.properties_.emplace_back(name, value);
    }
  }
  const json::Value* children = component.ArrayField(L"children");
  if (children != nullptr) {
    for (const json::Value& child : children->items()) {
      node.children_.push_back(BuildNode(core, child));
    }
  }
  return node;
}

}  // namespace

bool ComponentNode::Has(std::wstring_view name) const {
  return std::find_if(properties_.begin(), properties_.end(), [name](const auto& pair) {
           return pair.first == name;
         }) != properties_.end();
}

std::wstring ComponentNode::GetString(std::wstring_view name,
                                      std::wstring_view fallback) const {
  for (const auto& [key, value] : properties_) {
    if (key == name && value.is_string()) return value.AsString();
  }
  return std::wstring(fallback);
}

long long ComponentNode::GetInt(std::wstring_view name, long long fallback) const {
  for (const auto& [key, value] : properties_) {
    if (key == name && value.is_number()) {
      return static_cast<long long>(value.AsNumber());
    }
  }
  return fallback;
}

bool ComponentNode::GetBool(std::wstring_view name, bool fallback) const {
  for (const auto& [key, value] : properties_) {
    if (key == name && value.is_bool()) return value.AsBool();
  }
  return fallback;
}

const Route* ResolvedUiDocument::FindRoute(std::wstring_view id) const {
  for (const Route& route : routes_) {
    if (route.id == id) return &route;
  }
  return nullptr;
}

bool ResolvedUiDocument::Token(std::wstring_view theme, std::wstring_view name,
                               Rgba* out) const {
  for (std::size_t t = 0; t < theme_names_.size(); ++t) {
    if (theme_names_[t] != theme) continue;
    for (const auto& [token, color] : theme_tokens_[t]) {
      if (token == name) {
        if (out != nullptr) *out = color;
        return true;
      }
    }
  }
  return false;
}

core::Status ResolveDocument(const json::Value& core, const std::vector<ScreenSource>& sources,
                             std::vector<Diagnostic>* diagnostics,
                             std::unique_ptr<ResolvedUiDocument>* out) {
  if (out == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument, L"ResolveDocument requires an output");
  }
  std::vector<Diagnostic> local;
  std::vector<Diagnostic>& diags = diagnostics != nullptr ? *diagnostics : local;

  DHEPZ_RETURN_IF_ERROR(ValidateCore(core, &diags));

  // Merge in source order; a later source replaces a route wholesale
  // (embedded < override). Duplicate routes within one source are an error.
  std::vector<std::wstring> route_order;
  std::vector<const json::Value*> route_screen;
  std::vector<std::wstring> route_source;
  std::vector<json::Value> parsed_sources;
  std::wstring initial_route;

  for (const ScreenSource& source : sources) {
    json::Value document;
    const core::Status parsed = json::Parse(source.text, &document);
    if (!parsed.ok()) {
      Diagnostic diagnostic;
      std::wstring text;
      SplitParseMessage(parsed.Message(), &text, &diagnostic.line, &diagnostic.column);
      diagnostic.message = source.name + L": " + text;
      diags.push_back(diagnostic);
      continue;
    }
    parsed_sources.push_back(std::move(document));
    const json::Value& document_ref = parsed_sources.back();

    std::vector<Diagnostic> screen_diags;
    const core::Status validated = ValidateScreen(core, document_ref, &screen_diags);
    for (Diagnostic& diagnostic : screen_diags) {
      diagnostic.message = source.name + L": " + diagnostic.message;
      diags.push_back(diagnostic);
    }
    if (!validated.ok()) {
      continue;
    }

    const json::Value* components = document_ref.ArrayField(L"components");
    if (components == nullptr) continue;
    for (const json::Value& component : components->items()) {
      const std::wstring type = component.StringField(L"type");
      if (type == L"window") {
        const json::Value* route = component.Find(L"initial_route");
        if (route != nullptr && route->is_string()) {
          initial_route = route->AsString();
        }
        continue;
      }
      if (type != L"screen") continue;
      const std::wstring route_id = component.StringField(L"route_id");
      const auto known = std::find(route_order.begin(), route_order.end(), route_id);
      if (known != route_order.end()) {
        const std::size_t index = static_cast<std::size_t>(known - route_order.begin());
        if (route_source[index] == source.name) {
          const json::Value* at = component.Find(L"route_id");
          Diagnostic diagnostic;
          diagnostic.message = source.name + L": duplicate route '" + route_id + L"'";
          if (at != nullptr) {
            diagnostic.line = at->line();
            diagnostic.column = at->column();
          }
          diags.push_back(diagnostic);
        } else {
          route_screen[index] = &component;
          route_source[index] = source.name;
        }
        continue;
      }
      route_order.push_back(route_id);
      route_screen.push_back(&component);
      route_source.push_back(source.name);
    }
  }

  if (!diags.empty()) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"resolve produced " + std::to_wstring(diags.size()) + L" diagnostic(s)");
  }

  auto document = std::make_unique<ResolvedUiDocument>();
  const json::Value* tokens = core.ObjectField(L"tokens");
  if (tokens != nullptr) {
    for (const auto& [theme_name, theme] : tokens->members()) {
      document->theme_names_.push_back(theme_name);
      std::vector<std::pair<std::wstring, Rgba>> colors;
      if (theme.is_object()) {
        for (const auto& [token_name, color] : theme.members()) {
          Rgba rgba{};
          if (color.is_string() && ParseHexColor(color.AsString(), &rgba)) {
            colors.emplace_back(token_name, rgba);
          }
        }
      }
      document->theme_tokens_.push_back(std::move(colors));
    }
  }

  for (std::size_t i = 0; i < route_order.size(); ++i) {
    Route route;
    route.id = route_order[i];
    route.tab_label = route_screen[i]->StringField(L"tab_label", route_order[i]);
    route.show_in_tabs = route_screen[i]->BoolField(L"show_in_tabs", true);
    const json::Value* backdrop = route_screen[i]->Find(L"backdrop");
    if (backdrop != nullptr && backdrop->is_string() && !backdrop->AsString().empty()) {
      const std::wstring& value = backdrop->AsString();
      if (value.rfind(L"screen:", 0) == 0) {
        route.backdrop_kind = Route::BackdropKind::Screen;
        route.backdrop_value = value.substr(7);
      } else {
        const json::Value* dark = tokens != nullptr ? tokens->Find(L"dark") : nullptr;
        const bool is_token =
            dark != nullptr && dark->is_object() && dark->Find(value) != nullptr;
        route.backdrop_kind = is_token ? Route::BackdropKind::Color : Route::BackdropKind::Image;
        route.backdrop_value = value;
      }
    }
    route.root = BuildNode(core, *route_screen[i]);
    document->routes_.push_back(std::move(route));
  }

  for (const Route& route : document->routes_) {
    if (route.backdrop_kind == Route::BackdropKind::Screen &&
        document->FindRoute(route.backdrop_value) == nullptr) {
      return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                       L"backdrop screen '" + route.backdrop_value + L"' does not exist");
    }
  }

  if (initial_route.empty() && !document->routes_.empty()) {
    initial_route = document->routes_.front().id;
  }
  if (!initial_route.empty() && document->FindRoute(initial_route) == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"initial_route '" + initial_route + L"' does not exist");
  }
  document->initial_route_ = std::move(initial_route);

  *out = std::move(document);
  return core::Ok();
}

}  // namespace ui::config

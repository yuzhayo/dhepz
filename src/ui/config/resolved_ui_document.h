// The resolver's output (#53): an immutable document the render pipeline
// reads. Built once from the validated core catalog plus an ordered list of
// screen sources; later sources override earlier ones per route (embedded
// first, user override last), so merge precedence is embedded < override by
// construction.
//
// Immutability is structural: the only writer is the resolver, everything
// else holds a const pointer.
//
// This header stays free of windows.h.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/json.h"
#include "core/status.h"
#include "ui/config/ui_schema.h"

namespace ui::config {

struct Rgba {
  unsigned char r = 0;
  unsigned char g = 0;
  unsigned char b = 0;
  unsigned char a = 255;
};

// One raw screen document. `name` identifies the source in diagnostics
// (e.g. "embedded" or the override file path); `text` is the JSON.
struct ScreenSource {
  std::wstring name;
  std::wstring text;
};

class ResolvedUiDocument;

core::Status ResolveDocument(const json::Value& core, const std::vector<ScreenSource>& sources,
                             std::vector<Diagnostic>* diagnostics,
                             std::unique_ptr<ResolvedUiDocument>* out);

class ComponentNode {
 public:
  const std::wstring& type() const { return type_; }
  const std::wstring& id() const { return id_; }

  bool Has(std::wstring_view name) const;
  const json::Value* Property(std::wstring_view name) const;
  std::wstring GetString(std::wstring_view name, std::wstring_view fallback = {}) const;
  long long GetInt(std::wstring_view name, long long fallback = 0) const;
  bool GetBool(std::wstring_view name, bool fallback = false) const;
  const std::vector<ComponentNode>& children() const { return children_; }

  // Written by the resolver during construction; consumers receive const
  // references only, so immutability is enforced at the boundary.
  std::wstring type_;
  std::wstring id_;
  std::vector<std::pair<std::wstring, json::Value>> properties_;
  std::vector<ComponentNode> children_;
};

struct Route {
  std::wstring id;
  std::wstring tab_label;
  bool show_in_tabs = true;
  // The z-layer behind this screen's content (#62). A color token name, an
  // image path, or another route's id. When present, the screen's own fill
  // is transparent and this layer paints first.
  enum class BackdropKind { None, Color, Image, Screen };
  BackdropKind backdrop_kind = BackdropKind::None;
  std::wstring backdrop_value;
  ComponentNode root;
};

class ResolvedUiDocument {
 public:
  const std::vector<Route>& routes() const { return routes_; }
  const Route* FindRoute(std::wstring_view id) const;
  const std::wstring& initial_route() const { return initial_route_; }
  const std::vector<std::wstring>& themes() const { return theme_names_; }
  // Looks up a token colour in a theme; false when absent.
  bool Token(std::wstring_view theme, std::wstring_view name, Rgba* out) const;

 private:
  friend core::Status ResolveDocument(const json::Value&, const std::vector<ScreenSource>&,
                                      std::vector<Diagnostic>*,
                                      std::unique_ptr<ResolvedUiDocument>*);
  std::vector<Route> routes_;
  std::wstring initial_route_;
  std::vector<std::wstring> theme_names_;
  std::vector<std::vector<std::pair<std::wstring, Rgba>>> theme_tokens_;
};

// Resolves the core catalog plus the sources into an immutable document.
// Sources are merged in order: a route defined by a later source replaces
// the same route from an earlier one (embedded < override). Duplicate routes
// WITHIN one source are a diagnostic, as are syntax errors (with the
// parser's line/column) and every semantic error ValidateScreen reports.
core::Status ResolveDocument(const json::Value& core, const std::vector<ScreenSource>& sources,
                             std::vector<Diagnostic>* diagnostics,
                             std::unique_ptr<ResolvedUiDocument>* out);

}  // namespace ui::config

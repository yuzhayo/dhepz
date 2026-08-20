// Minimal JSON model for config-sized documents.
//
// Two properties are load-bearing for G3, so they are pinned by tests:
//
//   - Object member order is preserved. The UI resolver lays screens out in
//     declaration order, and rewriting a config must keep every field where
//     the user (or another tool) had it.
//   - Absent optional fields are not errors. The typed fallback accessors
//     (`StringField`, `BoolField`, `NumberField`) and `Find()` returning
//     nullptr are the plan's answer to optional config — no error plumbing
//     at every call site.
//
// The parser is strict, the way the tooling requires: no comments, no
// trailing commas, no garbage after the top-level value, so anything this
// layer accepts round-trips through ConvertFrom-Json on the PowerShell side.
// Nesting is bounded by kMaxDepth: a hostile or accidental 10,000-deep
// input is a ParseError, not a stack overflow.
//
// Duplicate member names keep the last value at the first key's position —
// standard JSON tolerance, with position stability for the resolver.
//
// core/ never includes Windows.h. This header is standard library only.
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/status.h"

namespace json {

// Deepest nesting the parser accepts. Real config is single-digit deep; the
// bound exists so adversarial input cannot trade depth for stack.
inline constexpr int kMaxDepth = 512;

enum class Type { Null, Bool, Number, String, Array, Object };

class Value {
 public:
  Value() = default;
  static Value Null();
  static Value Bool(bool value);
  static Value Number(double value);
  static Value Number(std::wstring raw, double value);
  static Value String(std::wstring value);
  static Value Array();
  static Value Object();

  Type type() const { return type_; }
  bool is_null() const { return type_ == Type::Null; }
  bool is_bool() const { return type_ == Type::Bool; }
  bool is_number() const { return type_ == Type::Number; }
  bool is_string() const { return type_ == Type::String; }
  bool is_array() const { return type_ == Type::Array; }
  bool is_object() const { return type_ == Type::Object; }

  bool AsBool(bool fallback = false) const;
  double AsNumber(double fallback = 0.0) const;
  const std::wstring& AsString() const;

  // Array access.
  std::size_t size() const { return items_.size(); }
  void Append(Value value);

  // Object access. Find returns nullptr when the key is absent.
  const Value* Find(std::wstring_view key) const;
  Value* Find(std::wstring_view key);
  std::wstring StringField(std::wstring_view key, std::wstring_view fallback = {}) const;
  bool BoolField(std::wstring_view key, bool fallback = false) const;
  double NumberField(std::wstring_view key, double fallback = 0.0) const;
  const Value* ArrayField(std::wstring_view key) const;
  const Value* ObjectField(std::wstring_view key) const;

  // Sets a member, keeping its original position when the key already exists.
  void Set(std::wstring_view key, Value value);
  void Remove(std::wstring_view key);

  const std::vector<std::pair<std::wstring, Value>>& members() const { return members_; }
  const std::vector<Value>& items() const { return items_; }

 private:
  Type type_ = Type::Null;
  bool bool_ = false;
  double number_ = 0.0;
  std::wstring text_;  // string payload, or verbatim number text
  std::vector<Value> items_;
  std::vector<std::pair<std::wstring, Value>> members_;
};

// Parses strict JSON from UTF-16 text. A leading U+FEFF (the BOM as a
// character) is tolerated and skipped. On failure the Status carries
// ParseError with a message naming what was expected plus a 1-based
// "(line N, column M)".
core::Status Parse(std::wstring_view text, Value* out);

// Parses UTF-8 input: converts strictly via str::FromUtf8 (invalid bytes are
// a ParseError, never a silent empty document), strips a UTF-8 BOM, then
// delegates to the UTF-16 parser.
core::Status ParseUtf8(std::string_view utf8, Value* out);

// Pretty-prints with two-space indent and a trailing newline.
std::wstring Serialize(const Value& value, bool pretty = true);

// Quotes and escapes a bare string as a JSON string literal.
std::wstring EscapeString(std::wstring_view value);

}  // namespace json

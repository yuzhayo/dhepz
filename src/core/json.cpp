#include "core/json.h"

#include <cmath>
#include <cstdio>
#include <cwchar>

namespace json {
namespace {

// Strict UTF-8 to UTF-16. core/ cannot call str::FromUtf8 — that conversion
// is built on a Windows API and lives a layer up — so the parser carries its
// own decoder with the same contract: invalid, truncated, overlong, and
// surrogate input is an error, never a silently empty document.
core::Status Utf8ToWide(std::string_view utf8, std::wstring* out) {
  out->clear();
  std::size_t i = 0;
  while (i < utf8.size()) {
    const unsigned char lead = static_cast<unsigned char>(utf8[i]);
    unsigned int code = 0;
    std::size_t length = 0;
    if (lead < 0x80) {
      code = lead;
      length = 1;
    } else if ((lead & 0xE0) == 0xC0) {
      code = lead & 0x1Fu;
      length = 2;
    } else if ((lead & 0xF0) == 0xE0) {
      code = lead & 0x0Fu;
      length = 3;
    } else if ((lead & 0xF8) == 0xF0) {
      code = lead & 0x07u;
      length = 4;
    } else {
      return DHEPZ_ERR(core::ErrorCode::ParseError, L"Input is not valid UTF-8");
    }
    if (i + length > utf8.size()) {
      return DHEPZ_ERR(core::ErrorCode::ParseError, L"Input is not valid UTF-8: truncated sequence");
    }
    for (std::size_t k = 1; k < length; ++k) {
      const unsigned char next = static_cast<unsigned char>(utf8[i + k]);
      if ((next & 0xC0) != 0x80) {
        return DHEPZ_ERR(core::ErrorCode::ParseError, L"Input is not valid UTF-8: bad continuation byte");
      }
      code = code * 64 + (next & 0x3Fu);
    }
    // Reject encodings a shorter form could express, the surrogate block,
    // and everything past the Unicode range.
    const unsigned int minimum =
        length == 2 ? 0x80u : length == 3 ? 0x800u : length == 4 ? 0x10000u : 0u;
    if (code < minimum || (code >= 0xD800 && code <= 0xDFFF) || code > 0x10FFFF) {
      return DHEPZ_ERR(core::ErrorCode::ParseError, L"Input is not valid UTF-8: illegal code point");
    }
    if (code >= 0x10000) {
      code -= 0x10000;
      out->push_back(static_cast<wchar_t>(0xD800 + (code >> 10)));
      out->push_back(static_cast<wchar_t>(0xDC00 + (code & 0x3FF)));
    } else {
      out->push_back(static_cast<wchar_t>(code));
    }
    i += length;
  }
  return core::Ok();
}

const std::wstring& EmptyString() {
  static const std::wstring instance;
  return instance;
}

class Parser {
 public:
  Parser(std::wstring_view text) : text_(text) {}

  bool Run(Value* out) {
    SkipWhitespace();
    if (!ParseValue(out)) {
      return false;
    }
    SkipWhitespace();
    if (pos_ != text_.size()) {
      Fail(L"Unexpected text after the top-level value");
      return false;
    }
    return true;
  }

  const std::wstring& error() const { return error_; }

 private:
  std::wstring_view text_;
  std::size_t pos_ = 0;
  int depth_ = 0;
  std::wstring error_;

  std::wstring Message(std::wstring_view what) const {
    int line = 0;
    int column = 0;
    PositionAt(pos_, &line, &column);
    return std::wstring(what) + L" (line " + std::to_wstring(line) + L", column " +
           std::to_wstring(column) + L")";
  }

  void PositionAt(std::size_t at, int* line, int* column) const {
    std::size_t current_line = 1;
    std::size_t current_column = 1;
    for (std::size_t i = 0; i < at && i < text_.size(); ++i) {
      if (text_[i] == L'\n') {
        ++current_line;
        current_column = 1;
      } else {
        ++current_column;
      }
    }
    *line = static_cast<int>(current_line);
    *column = static_cast<int>(current_column);
  }

  bool Fail(std::wstring_view what) {
    if (error_.empty()) {
      error_ = Message(what);
    }
    return false;
  }

  void SkipWhitespace() {
    while (pos_ < text_.size()) {
      const wchar_t c = text_[pos_];
      if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') {
        ++pos_;
      } else {
        break;
      }
    }
  }

  bool Literal(std::wstring_view word) {
    if (text_.compare(pos_, word.size(), word) != 0) {
      return false;
    }
    pos_ += word.size();
    return true;
  }

  bool ParseValue(Value* out) {
    if (pos_ >= text_.size()) {
      return Fail(L"Unexpected end of input");
    }
    const std::size_t start = pos_;
    bool parsed = false;
    switch (text_[pos_]) {
      case L'{':
        parsed = ParseObject(out);
        break;
      case L'[':
        parsed = ParseArray(out);
        break;
      case L'"': {
        std::wstring text;
        if (!ParseString(&text)) {
          return false;
        }
        *out = Value::String(std::move(text));
        parsed = true;
        break;
      }
      case L't':
        if (!Literal(L"true")) {
          return Fail(L"Invalid literal, expected 'true'");
        }
        *out = Value::Bool(true);
        parsed = true;
        break;
      case L'f':
        if (!Literal(L"false")) {
          return Fail(L"Invalid literal, expected 'false'");
        }
        *out = Value::Bool(false);
        parsed = true;
        break;
      case L'n':
        if (!Literal(L"null")) {
          return Fail(L"Invalid literal, expected 'null'");
        }
        *out = Value::Null();
        parsed = true;
        break;
      default:
        parsed = ParseNumber(out);
        break;
    }
    if (parsed) {
      int line = 0;
      int column = 0;
      PositionAt(start, &line, &column);
      out->SetSourcePosition(line, column);
    }
    return parsed;
  }

  bool ParseObject(Value* out) {
    if (depth_ >= kMaxDepth) {
      return Fail(L"JSON nesting is too deep (limit is 512)");
    }
    ++pos_;  // '{'
    ++depth_;
    Value object = Value::Object();
    SkipWhitespace();
    if (pos_ < text_.size() && text_[pos_] == L'}') {
      ++pos_;
      --depth_;
      *out = std::move(object);
      return true;
    }
    for (;;) {
      SkipWhitespace();
      if (pos_ >= text_.size() || text_[pos_] != L'"') {
        return Fail(L"Expected a quoted member name");
      }
      std::wstring key;
      const std::size_t key_start = pos_;
      if (!ParseString(&key)) {
        return false;
      }
      SkipWhitespace();
      if (pos_ >= text_.size() || text_[pos_] != L':') {
        return Fail(L"Expected ':' after the member name");
      }
      ++pos_;
      SkipWhitespace();
      Value member;
      if (!ParseValue(&member)) {
        return false;
      }
      // Diagnostics about this member should point at the name, not at the
      // value: stamp the key's position over the value's own.
      int key_line = 0;
      int key_column = 0;
      PositionAt(key_start, &key_line, &key_column);
      member.SetSourcePosition(key_line, key_column);
      object.Set(key, std::move(member));
      SkipWhitespace();
      if (pos_ < text_.size() && text_[pos_] == L',') {
        ++pos_;
        continue;
      }
      if (pos_ < text_.size() && text_[pos_] == L'}') {
        ++pos_;
        --depth_;
        *out = std::move(object);
        return true;
      }
      return Fail(L"Expected ',' or '}'");
    }
  }

  bool ParseArray(Value* out) {
    if (depth_ >= kMaxDepth) {
      return Fail(L"JSON nesting is too deep (limit is 512)");
    }
    ++pos_;  // '['
    ++depth_;
    Value array = Value::Array();
    SkipWhitespace();
    if (pos_ < text_.size() && text_[pos_] == L']') {
      ++pos_;
      --depth_;
      *out = std::move(array);
      return true;
    }
    for (;;) {
      SkipWhitespace();
      Value item;
      if (!ParseValue(&item)) {
        return false;
      }
      array.Append(std::move(item));
      SkipWhitespace();
      if (pos_ < text_.size() && text_[pos_] == L',') {
        ++pos_;
        continue;
      }
      if (pos_ < text_.size() && text_[pos_] == L']') {
        ++pos_;
        --depth_;
        *out = std::move(array);
        return true;
      }
      return Fail(L"Expected ',' or ']'");
    }
  }

  bool ParseString(std::wstring* out) {
    ++pos_;  // opening quote
    out->clear();
    while (pos_ < text_.size()) {
      const wchar_t c = text_[pos_++];
      if (c == L'"') {
        return true;
      }
      if (c == L'\\') {
        if (pos_ >= text_.size()) {
          break;
        }
        const wchar_t esc = text_[pos_++];
        switch (esc) {
          case L'"': out->push_back(L'"'); break;
          case L'\\': out->push_back(L'\\'); break;
          case L'/': out->push_back(L'/'); break;
          case L'b': out->push_back(L'\b'); break;
          case L'f': out->push_back(L'\f'); break;
          case L'n': out->push_back(L'\n'); break;
          case L'r': out->push_back(L'\r'); break;
          case L't': out->push_back(L'\t'); break;
          case L'u': {
            if (pos_ + 4 > text_.size()) {
              return Fail(L"Truncated \\u escape");
            }
            unsigned int code = 0;
            for (int i = 0; i < 4; ++i) {
              const wchar_t digit = text_[pos_ + i];
              unsigned int nibble = 0;
              if (digit >= L'0' && digit <= L'9') {
                nibble = static_cast<unsigned int>(digit - L'0');
              } else if (digit >= L'a' && digit <= L'f') {
                nibble = static_cast<unsigned int>(digit - L'a') + 10;
              } else if (digit >= L'A' && digit <= L'F') {
                nibble = static_cast<unsigned int>(digit - L'A') + 10;
              } else {
                return Fail(L"Invalid \\u escape");
              }
              code = code * 16 + nibble;
            }
            pos_ += 4;
            // A high surrogate must be followed by a low one; pairing them
            // here keeps the UTF-16 output well-formed for anything that
            // later converts it back to UTF-8.
            if (code >= 0xD800 && code <= 0xDBFF && pos_ + 6 <= text_.size() &&
                text_[pos_] == L'\\' && text_[pos_ + 1] == L'u') {
              unsigned int low = 0;
              bool valid = true;
              for (int i = 0; i < 4; ++i) {
                const wchar_t digit = text_[pos_ + 2 + i];
                unsigned int nibble = 0;
                if (digit >= L'0' && digit <= L'9') {
                  nibble = static_cast<unsigned int>(digit - L'0');
                } else if (digit >= L'a' && digit <= L'f') {
                  nibble = static_cast<unsigned int>(digit - L'a') + 10;
                } else if (digit >= L'A' && digit <= L'F') {
                  nibble = static_cast<unsigned int>(digit - L'A') + 10;
                } else {
                  valid = false;
                  break;
                }
                low = low * 16 + nibble;
              }
              if (valid && low >= 0xDC00 && low <= 0xDFFF) {
                pos_ += 6;
                out->push_back(static_cast<wchar_t>(code));
                out->push_back(static_cast<wchar_t>(low));
                break;
              }
            }
            out->push_back(static_cast<wchar_t>(code));
            break;
          }
          default:
            return Fail(L"Invalid escape sequence");
        }
        continue;
      }
      if (c < 0x20) {
        return Fail(L"Control character inside a string");
      }
      out->push_back(c);
    }
    return Fail(L"Unterminated string");
  }

  bool ParseNumber(Value* out) {
    const std::size_t start = pos_;
    if (pos_ < text_.size() && text_[pos_] == L'-') {
      ++pos_;
    }
    bool any_digit = false;
    while (pos_ < text_.size() && text_[pos_] >= L'0' && text_[pos_] <= L'9') {
      ++pos_;
      any_digit = true;
    }
    // A fraction is only valid after an integer digit, and must have digits.
    if (any_digit && pos_ < text_.size() && text_[pos_] == L'.') {
      ++pos_;
      bool frac_digit = false;
      while (pos_ < text_.size() && text_[pos_] >= L'0' && text_[pos_] <= L'9') {
        ++pos_;
        frac_digit = true;
      }
      if (!frac_digit) {
        pos_ = start;
        return Fail(L"Missing digits after decimal point");
      }
    }
    if (any_digit && pos_ < text_.size() && (text_[pos_] == L'e' || text_[pos_] == L'E')) {
      ++pos_;
      if (pos_ < text_.size() && (text_[pos_] == L'-' || text_[pos_] == L'+')) {
        ++pos_;
      }
      bool exp_digit = false;
      while (pos_ < text_.size() && text_[pos_] >= L'0' && text_[pos_] <= L'9') {
        ++pos_;
        exp_digit = true;
      }
      if (!exp_digit) {
        return Fail(L"Invalid exponent");
      }
    }
    if (!any_digit) {
      pos_ = start;
      return Fail(L"Unexpected character");
    }
    const std::wstring raw(text_.substr(start, pos_ - start));
    *out = Value::Number(raw, std::wcstod(raw.c_str(), nullptr));
    return true;
  }
};

void Write(const Value& value, bool pretty, int indent, std::wstring* out) {
  const std::wstring pad = pretty ? std::wstring(static_cast<std::size_t>(indent) * 2, L' ')
                                   : std::wstring();
  const std::wstring pad_inner =
      pretty ? std::wstring(static_cast<std::size_t>(indent + 1) * 2, L' ') : std::wstring();
  const wchar_t* newline = pretty ? L"\n" : L"";
  const wchar_t* colon = pretty ? L": " : L":";

  switch (value.type()) {
    case Type::Null:
      out->append(L"null");
      break;
    case Type::Bool:
      out->append(value.AsBool() ? L"true" : L"false");
      break;
    case Type::Number: {
      const std::wstring& raw = value.AsString();
      if (!raw.empty()) {
        out->append(raw);
        break;
      }
      const double number = value.AsNumber();
      wchar_t buffer[64];
      if (number == std::floor(number) && std::fabs(number) < 1e15) {
        std::swprintf(buffer, 64, L"%lld", static_cast<long long>(number));
      } else {
        std::swprintf(buffer, 64, L"%.17g", number);
      }
      out->append(buffer);
      break;
    }
    case Type::String:
      out->append(EscapeString(value.AsString()));
      break;
    case Type::Array: {
      if (value.items().empty()) {
        out->append(L"[]");
        break;
      }
      out->append(L"[").append(newline);
      for (std::size_t i = 0; i < value.items().size(); ++i) {
        out->append(pad_inner);
        Write(value.items()[i], pretty, indent + 1, out);
        if (i + 1 < value.items().size()) {
          out->append(L",");
        }
        out->append(newline);
      }
      out->append(pad).append(L"]");
      break;
    }
    case Type::Object: {
      if (value.members().empty()) {
        out->append(L"{}");
        break;
      }
      out->append(L"{").append(newline);
      for (std::size_t i = 0; i < value.members().size(); ++i) {
        out->append(pad_inner);
        out->append(EscapeString(value.members()[i].first));
        out->append(colon);
        Write(value.members()[i].second, pretty, indent + 1, out);
        if (i + 1 < value.members().size()) {
          out->append(L",");
        }
        out->append(newline);
      }
      out->append(pad).append(L"}");
      break;
    }
  }
}

}  // namespace

Value Value::Null() { return Value(); }

Value Value::Bool(bool value) {
  Value out;
  out.type_ = Type::Bool;
  out.bool_ = value;
  return out;
}

Value Value::Number(double value) {
  Value out;
  out.type_ = Type::Number;
  out.number_ = value;
  return out;
}

Value Value::Number(std::wstring raw, double value) {
  Value out;
  out.type_ = Type::Number;
  out.number_ = value;
  out.text_ = std::move(raw);
  return out;
}

Value Value::String(std::wstring value) {
  Value out;
  out.type_ = Type::String;
  out.text_ = std::move(value);
  return out;
}

Value Value::Array() {
  Value out;
  out.type_ = Type::Array;
  return out;
}

Value Value::Object() {
  Value out;
  out.type_ = Type::Object;
  return out;
}

bool Value::AsBool(bool fallback) const { return type_ == Type::Bool ? bool_ : fallback; }
double Value::AsNumber(double fallback) const { return type_ == Type::Number ? number_ : fallback; }

const std::wstring& Value::AsString() const {
  if (type_ == Type::String || type_ == Type::Number) {
    return text_;
  }
  return EmptyString();
}

void Value::Append(Value value) {
  if (type_ != Type::Array) {
    type_ = Type::Array;
    members_.clear();
  }
  items_.push_back(std::move(value));
}

const Value* Value::Find(std::wstring_view key) const {
  if (type_ != Type::Object) {
    return nullptr;
  }
  for (const auto& member : members_) {
    if (member.first == key) {
      return &member.second;
    }
  }
  return nullptr;
}

Value* Value::Find(std::wstring_view key) {
  if (type_ != Type::Object) {
    return nullptr;
  }
  for (auto& member : members_) {
    if (member.first == key) {
      return &member.second;
    }
  }
  return nullptr;
}

std::wstring Value::StringField(std::wstring_view key, std::wstring_view fallback) const {
  const Value* found = Find(key);
  if (found == nullptr || !found->is_string()) {
    return std::wstring(fallback);
  }
  return found->AsString();
}

bool Value::BoolField(std::wstring_view key, bool fallback) const {
  const Value* found = Find(key);
  if (found == nullptr || !found->is_bool()) {
    return fallback;
  }
  return found->AsBool();
}

double Value::NumberField(std::wstring_view key, double fallback) const {
  const Value* found = Find(key);
  if (found == nullptr || !found->is_number()) {
    return fallback;
  }
  return found->AsNumber();
}

const Value* Value::ArrayField(std::wstring_view key) const {
  const Value* found = Find(key);
  return (found != nullptr && found->is_array()) ? found : nullptr;
}

const Value* Value::ObjectField(std::wstring_view key) const {
  const Value* found = Find(key);
  return (found != nullptr && found->is_object()) ? found : nullptr;
}

void Value::Set(std::wstring_view key, Value value) {
  if (type_ != Type::Object) {
    type_ = Type::Object;
    items_.clear();
  }
  for (auto& member : members_) {
    if (member.first == key) {
      member.second = std::move(value);
      return;
    }
  }
  members_.emplace_back(std::wstring(key), std::move(value));
}

void Value::Remove(std::wstring_view key) {
  for (std::size_t i = 0; i < members_.size(); ++i) {
    if (members_[i].first == key) {
      members_.erase(members_.begin() + static_cast<std::ptrdiff_t>(i));
      return;
    }
  }
}

core::Status Parse(std::wstring_view text, Value* out) {
  if (out == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument, L"Parse requires an output value");
  }
  // A leading U+FEFF is the BOM as a character — tolerated and skipped, per
  // the division pinned by #7/#9: converters preserve it, this layer strips.
  if (!text.empty() && text.front() == L'\uFEFF') {
    text.remove_prefix(1);
  }
  Parser parser(text);
  Value parsed;
  if (!parser.Run(&parsed)) {
    const std::wstring& message = parser.error();
    return DHEPZ_ERR(core::ErrorCode::ParseError, message.empty() ? L"Invalid JSON" : message);
  }
  *out = std::move(parsed);
  return core::Ok();
}

core::Status ParseUtf8(std::string_view utf8, Value* out) {
  if (out == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument, L"ParseUtf8 requires an output value");
  }
  if (utf8.size() >= 3 && static_cast<unsigned char>(utf8[0]) == 0xEF &&
      static_cast<unsigned char>(utf8[1]) == 0xBB && static_cast<unsigned char>(utf8[2]) == 0xBF) {
    utf8.remove_prefix(3);
  }
  std::wstring wide;
  DHEPZ_RETURN_IF_ERROR(Utf8ToWide(utf8, &wide));
  return Parse(wide, out);
}

std::wstring Serialize(const Value& value, bool pretty) {
  std::wstring out;
  Write(value, pretty, 0, &out);
  if (pretty) {
    out.push_back(L'\n');
  }
  return out;
}

std::wstring EscapeString(std::wstring_view value) {
  std::wstring out;
  out.reserve(value.size() + 2);
  out.push_back(L'"');
  for (wchar_t c : value) {
    switch (c) {
      case L'"': out.append(L"\\\""); break;
      case L'\\': out.append(L"\\\\"); break;
      case L'\b': out.append(L"\\b"); break;
      case L'\f': out.append(L"\\f"); break;
      case L'\n': out.append(L"\\n"); break;
      case L'\r': out.append(L"\\r"); break;
      case L'\t': out.append(L"\\t"); break;
      default:
        if (c < 0x20) {
          wchar_t buffer[8];
          std::swprintf(buffer, 8, L"\\u%04x", static_cast<unsigned int>(c));
          out.append(buffer);
        } else {
          out.push_back(c);
        }
    }
  }
  out.push_back(L'"');
  return out;
}

}  // namespace json

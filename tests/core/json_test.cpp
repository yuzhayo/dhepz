#include "core/json.h"

#include <string>

#include "framework/test_case.h"

namespace {

// Parses and fails the test with the parser's own message if it cannot.
json::Value ParseOrDie(const std::wstring& text, const char* file, int line) {
  json::Value value;
  const core::Status status = json::Parse(text, &value);
  if (!status.ok()) {
    ::testing::Fail(std::string("expected parse to succeed\n    error: ") +
                        ::testing::Describe(std::wstring_view(status.Message())),
                    file, line);
  }
  return value;
}

core::Status ParseExpectingFailure(const std::wstring& text, json::Value* out) {
  return json::Parse(text, out);
}

}  // namespace

#define PARSE_OR_DIE(text) ParseOrDie((text), __FILE__, __LINE__)

DHEPZ_TEST(Json, ParsesEveryScalarType) {
  const json::Value value = PARSE_OR_DIE(L"{\"s\": \"text\", \"n\": -12.5e2, \"t\": true, \"f\": false, \"z\": null}");
  DHEPZ_CHECK(value.is_object());
  DHEPZ_CHECK_EQ(value.StringField(L"s"), std::wstring(L"text"));
  DHEPZ_CHECK_EQ(value.NumberField(L"n"), -1250.0);
  DHEPZ_CHECK(value.BoolField(L"t"));
  DHEPZ_CHECK_FALSE(value.BoolField(L"f"));
  const json::Value* z = value.Find(L"z");
  DHEPZ_CHECK(z != nullptr);
  DHEPZ_CHECK(z->is_null());
}

// The UI resolver lays screens out in declaration order, so this is the
// load-bearing behaviour for G3.
DHEPZ_TEST(Json, PreservesMemberOrder) {
  const json::Value value = PARSE_OR_DIE(L"{\"zeta\": 1, \"alpha\": 2, \"mid\": 3}");
  DHEPZ_CHECK_EQ(value.members().size(), static_cast<std::size_t>(3));
  DHEPZ_CHECK_EQ(value.members()[0].first, std::wstring(L"zeta"));
  DHEPZ_CHECK_EQ(value.members()[1].first, std::wstring(L"alpha"));
  DHEPZ_CHECK_EQ(value.members()[2].first, std::wstring(L"mid"));
}

DHEPZ_TEST(Json, DuplicateKeyKeepsLastValueAtFirstPosition) {
  const json::Value value = PARSE_OR_DIE(L"{\"a\": 1, \"b\": 2, \"a\": 3}");
  DHEPZ_CHECK_EQ(value.members().size(), static_cast<std::size_t>(2));
  DHEPZ_CHECK_EQ(value.members()[0].first, std::wstring(L"a"));
  DHEPZ_CHECK_EQ(value.NumberField(L"a"), 3.0);
  DHEPZ_CHECK_EQ(value.members()[1].first, std::wstring(L"b"));
}

DHEPZ_TEST(Json, FallbackAccessIsNotAnError) {
  const json::Value value = PARSE_OR_DIE(L"{\"present\": \"yes\"}");
  DHEPZ_CHECK_EQ(value.StringField(L"missing", L"default"), std::wstring(L"default"));
  DHEPZ_CHECK(value.BoolField(L"missing", true));
  DHEPZ_CHECK_EQ(value.NumberField(L"missing", 7.5), 7.5);
  DHEPZ_CHECK(value.Find(L"missing") == nullptr);
  DHEPZ_CHECK(value.ArrayField(L"present") == nullptr);  // wrong type, not a crash
  // A field of the wrong type falls back too.
  DHEPZ_CHECK_EQ(value.StringField(L"present", L"x"), std::wstring(L"yes"));
  DHEPZ_CHECK_EQ(value.NumberField(L"present", 1.0), 1.0);
}

DHEPZ_TEST(Json, ErrorsCarryLineColumnAndExpectation) {
  json::Value value;
  // Line 2, the missing colon after the member name.
  const core::Status status = ParseExpectingFailure(L"{\n  \"key\" 1\n}", &value);
  DHEPZ_CHECK_FALSE(status.ok());
  DHEPZ_CHECK_EQ(status.Code(), core::ErrorCode::ParseError);
  DHEPZ_CHECK_CONTAINS(status.Message(), std::wstring(L"line 2"));
  DHEPZ_CHECK_CONTAINS(status.Message(), std::wstring(L"column 9"));
  DHEPZ_CHECK_CONTAINS(status.Message(), std::wstring(L"':'"));
}

DHEPZ_TEST(Json, RejectsTrailingCommas) {
  json::Value value;
  DHEPZ_CHECK_FALSE(json::Parse(L"{\"a\": 1,}", &value).ok());
  DHEPZ_CHECK_FALSE(json::Parse(L"[1, 2,]", &value).ok());
}

// No comments, so whatever this layer accepts round-trips through
// ConvertFrom-Json on the tooling side.
DHEPZ_TEST(Json, RejectsCommentsAndGarbage) {
  json::Value value;
  DHEPZ_CHECK_FALSE(json::Parse(L"// no comments\n{}", &value).ok());
  DHEPZ_CHECK_FALSE(json::Parse(L"{\"a\": 1} /* trailing */", &value).ok());
  const core::Status trailing = json::Parse(L"{} {}", &value);
  DHEPZ_CHECK_FALSE(trailing.ok());
  DHEPZ_CHECK_CONTAINS(trailing.Message(), std::wstring(L"after the top-level value"));
}

DHEPZ_TEST(Json, RejectsMalformedLiteralsAndNumbers) {
  json::Value value;
  DHEPZ_CHECK_FALSE(json::Parse(L"tru", &value).ok());
  DHEPZ_CHECK_FALSE(json::Parse(L"1.", &value).ok());
  DHEPZ_CHECK_FALSE(json::Parse(L"1e", &value).ok());
  DHEPZ_CHECK_FALSE(json::Parse(L"\"unterminated", &value).ok());
  DHEPZ_CHECK_FALSE(json::Parse(L"", &value).ok());
  // Split literals stop the hex escape swallowing the following 'c'.
  const core::Status control = json::Parse(L"\"bad\x01" L"char\"", &value);
  DHEPZ_CHECK_FALSE(control.ok());
  DHEPZ_CHECK_CONTAINS(control.Message(), std::wstring(L"Control character"));
}

// The mandated verification: 10,000-deep input is an error, not a crash.
DHEPZ_TEST(Json, DepthLimitRejectsTenThousandNesting) {
  std::wstring deep(10000, L'[');
  deep.append(10000, L']');
  json::Value value;
  const core::Status status = json::Parse(deep, &value);
  DHEPZ_CHECK_FALSE(status.ok());
  DHEPZ_CHECK_EQ(status.Code(), core::ErrorCode::ParseError);
  DHEPZ_CHECK_CONTAINS(status.Message(), std::wstring(L"too deep"));

  // Same shape as objects.
  std::wstring obj(10000 * 4, L'{');
  json::Value value2;
  DHEPZ_CHECK_FALSE(json::Parse(obj, &value2).ok());
}

DHEPZ_TEST(Json, DepthLimitAllowsDeepButSaneNesting) {
  // Well under kMaxDepth: parses fine, proving the limit is not hair-trigger.
  std::wstring nested;
  for (int i = 0; i < 100; ++i) {
    nested += L"{\"next\": ";
  }
  nested += L"1";
  for (int i = 0; i < 100; ++i) {
    nested += L"}";
  }
  const json::Value value = PARSE_OR_DIE(nested);
  DHEPZ_CHECK(value.is_object());
}

DHEPZ_TEST(Json, StringEscapesIncludingSurrogatePairs) {
  const json::Value value =
      PARSE_OR_DIE(L"\"a\\\"b\\\\c\\/d\\b\\f\\n\\r\\t\\u00e9\\uD83D\\uDE00\"");
  const std::wstring expected = L"a\"b\\c/d\b\f\n\r\t\u00e9\U0001F600";
  DHEPZ_CHECK_EQ(value.AsString(), expected);

  json::Value bad;
  DHEPZ_CHECK_FALSE(json::Parse(L"\"\\q\"", &bad).ok());
  DHEPZ_CHECK_FALSE(json::Parse(L"\"\\u12\"", &bad).ok());
}

DHEPZ_TEST(Json, BomIsToleratedAndStripped) {
  // Wide BOM character in front of the document.
  const json::Value wide = PARSE_OR_DIE(L"\uFEFF{\"a\": 1}");
  DHEPZ_CHECK_EQ(wide.NumberField(L"a"), 1.0);

  // UTF-8 BOM bytes on the UTF-8 entry point.
  json::Value utf8;
  DHEPZ_CHECK(json::ParseUtf8(std::string("\xEF\xBB\xBF") + "{\"a\": 2}", &utf8).ok());
  DHEPZ_CHECK_EQ(utf8.NumberField(L"a"), 2.0);
}

DHEPZ_TEST(Json, Utf8InputRoundTripsAndRejectsBadBytes) {
  json::Value value;
  DHEPZ_CHECK(json::ParseUtf8("{\"note\": \"caf\xC3\xA9 \xE4\xB8\xAD \xF0\x9F\x98\x80\"}", &value).ok());
  DHEPZ_CHECK_EQ(value.StringField(L"note"), std::wstring(L"caf\u00e9 \u4e2d \U0001F600"));

  // The old build's failure mode: one bad byte must be a ParseError, never a
  // silent empty document.
  json::Value bad;
  const core::Status truncated = json::ParseUtf8(std::string("\xC3\x28{}"), &bad);
  DHEPZ_CHECK_FALSE(truncated.ok());
  DHEPZ_CHECK_EQ(truncated.Code(), core::ErrorCode::ParseError);

  json::Value overlong;
  DHEPZ_CHECK_FALSE(json::ParseUtf8(std::string("\xC0\xAF{}"), &overlong).ok());
}

DHEPZ_TEST(Json, SerializeRoundTripsAndKeepsOrder) {
  const json::Value value = PARSE_OR_DIE(L"{\"z\": [1, 2.5, \"x\"], \"a\": {\"b\": null}}");
  const std::wstring pretty = json::Serialize(value);
  DHEPZ_CHECK(pretty.back() == L'\n');
  const json::Value reparsed = PARSE_OR_DIE(pretty);
  DHEPZ_CHECK_EQ(reparsed.members()[0].first, std::wstring(L"z"));
  DHEPZ_CHECK_EQ(reparsed.members()[1].first, std::wstring(L"a"));
  DHEPZ_CHECK_EQ(reparsed.ObjectField(L"a")->Find(L"b")->is_null(), true);

  // Compact form is one line and parses back to the same shape.
  const std::wstring compact = json::Serialize(value, false);
  DHEPZ_CHECK(compact.find(L'\n') == std::wstring::npos);
  const json::Value compact_reparsed = PARSE_OR_DIE(compact);
  DHEPZ_CHECK_EQ(compact_reparsed.ArrayField(L"z")->size(), static_cast<std::size_t>(3));
}

DHEPZ_TEST(Json, NumbersKeepTheirVerbatimText) {
  const json::Value value = PARSE_OR_DIE(L"{\"n\": 1.500}");
  const json::Value* n = value.Find(L"n");
  DHEPZ_CHECK(n != nullptr);
  DHEPZ_CHECK_EQ(n->AsNumber(), 1.5);
  DHEPZ_CHECK_EQ(n->AsString(), std::wstring(L"1.500"));
  // Serialize writes the verbatim form back, so a rewrite does not reformat
  // numbers the user typed.
  DHEPZ_CHECK_CONTAINS(json::Serialize(value, false), std::wstring(L"1.500"));
}

DHEPZ_TEST(Json, MutatorsKeepPositionSemantics) {
  json::Value object = json::Value::Object();
  object.Set(L"b", json::Value::Number(1.0));
  object.Set(L"a", json::Value::Number(2.0));
  object.Set(L"b", json::Value::Number(3.0));  // replace in place, no move
  DHEPZ_CHECK_EQ(object.members()[0].first, std::wstring(L"b"));
  DHEPZ_CHECK_EQ(object.NumberField(L"b"), 3.0);
  object.Remove(L"b");
  DHEPZ_CHECK_EQ(object.members().size(), static_cast<std::size_t>(1));
  DHEPZ_CHECK(object.Find(L"b") == nullptr);
}

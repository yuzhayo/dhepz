#include "core/json.h"

#include "framework/test_case.h"

DHEPZ_TEST(JsonPosition, ValuesCarryTheirSourceLineAndColumn) {
  json::Value root;
  DHEPZ_CHECK(json::Parse(L"{\n  \"a\": [\n    1\n  ],\n  \"b\": true\n}\n", &root).ok());
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(root.line()), 1ull);

  const json::Value* a = root.Find(L"a");
  DHEPZ_CHECK(a != nullptr);
  // Member values carry the KEY's position.
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(a->line()), 2ull);
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(a->column()), 3ull);
  // Array items keep their own position.
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(a->items()[0].line()), 3ull);

  const json::Value* b = root.Find(L"b");
  DHEPZ_CHECK(b != nullptr);
  DHEPZ_CHECK_EQ(static_cast<unsigned long long>(b->line()), 5ull);
}

DHEPZ_TEST(JsonPosition, CodeBuiltValuesHaveNoPosition) {
  const json::Value made = json::Value::Bool(true);
  DHEPZ_CHECK_EQ(made.line(), 0);
  DHEPZ_CHECK_EQ(made.column(), 0);
}

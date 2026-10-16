#include <cstddef>
#include <string>
#include <string_view>

#include "core/json.h"
#include "framework/test_case.h"
#include "modules/opencode/opencode_sessions.h"

namespace {

// A trimmed real-world response: one session with the full metadata shape the
// API returns, one minimal session that only carries an id and title, plus
// the paging cursor the server appends. The projection must tolerate all of
// it — absent fields become empty cells, never errors.
constexpr std::wstring_view kApiResponse = LR"({
  "data": [
    {
      "id": "ses_alpha",
      "projectID": "global",
      "agent": "build",
      "title": "Casual greeting check-in",
      "time": { "created": 1787714463527, "updated": 1792155239049, "idle": 1792153987374 },
      "location": { "directory": "C:\\VSCODE" }
    },
    { "id": "ses_beta", "title": "Second" }
  ],
  "cursor": { "next": "eyJhbmNob3IifQ" }
})";

DHEPZ_TEST(OpencodeSessions, ProjectsTheApiListIntoTheExportDocument) {
  std::wstring document;
  std::size_t total = 0;
  const core::Status built = modules::opencode::BuildSessionsDocument(
      kApiResponse, L"2026-10-16 17:00:00", &document, &total);
  DHEPZ_CHECK(built.ok());
  DHEPZ_CHECK_EQ(total, std::size_t{2});

  json::Value parsed;
  DHEPZ_CHECK(json::Parse(document, &parsed).ok());
  DHEPZ_CHECK_EQ(parsed.StringField(L"generated"),
                 std::wstring(L"2026-10-16 17:00:00"));
  DHEPZ_CHECK_EQ(parsed.StringField(L"source"),
                 std::wstring(L"opencode api get /api/session"));
  DHEPZ_CHECK_EQ(parsed.NumberField(L"total"), 2.0);

  const json::Value* sessions = parsed.ArrayField(L"sessions");
  DHEPZ_CHECK(sessions != nullptr);
  if (sessions == nullptr) return;
  DHEPZ_CHECK_EQ(sessions->size(), std::size_t{2});

  const json::Value& first = sessions->items().front();
  DHEPZ_CHECK_EQ(first.StringField(L"id"), std::wstring(L"ses_alpha"));
  DHEPZ_CHECK_EQ(first.StringField(L"title"),
                 std::wstring(L"Casual greeting check-in"));
  DHEPZ_CHECK_EQ(first.StringField(L"directory"), std::wstring(L"C:\\VSCODE"));
  DHEPZ_CHECK_EQ(first.StringField(L"project"), std::wstring(L"global"));
  DHEPZ_CHECK_EQ(first.StringField(L"agent"), std::wstring(L"build"));
  DHEPZ_CHECK_EQ(static_cast<long long>(first.NumberField(L"time_updated_ms")),
                 1792155239049LL);
  DHEPZ_CHECK(!first.StringField(L"time_created").empty());
  DHEPZ_CHECK(!first.StringField(L"time_updated").empty());

  const json::Value& second = sessions->items().back();
  DHEPZ_CHECK_EQ(second.StringField(L"id"), std::wstring(L"ses_beta"));
  DHEPZ_CHECK_EQ(second.StringField(L"directory"), std::wstring());
  DHEPZ_CHECK_EQ(second.NumberField(L"time_updated_ms"), 0.0);
}

DHEPZ_TEST(OpencodeSessions, WritesAnEmptyDocumentWhenThereAreNoSessions) {
  std::wstring document;
  std::size_t total = 0;
  const core::Status built = modules::opencode::BuildSessionsDocument(
      LR"({"data": []})", L"now", &document, &total);
  DHEPZ_CHECK(built.ok());
  DHEPZ_CHECK_EQ(total, std::size_t{0});

  json::Value parsed;
  DHEPZ_CHECK(json::Parse(document, &parsed).ok());
  const json::Value* sessions = parsed.ArrayField(L"sessions");
  DHEPZ_CHECK(sessions != nullptr);
  if (sessions != nullptr) {
    DHEPZ_CHECK_EQ(sessions->size(), std::size_t{0});
  }
}

DHEPZ_TEST(OpencodeSessions, RejectsAResponseWithoutADataArray) {
  std::wstring document;
  std::size_t total = 0;
  const core::Status built = modules::opencode::BuildSessionsDocument(
      LR"({"error": "service unavailable"})", L"now", &document, &total);
  DHEPZ_CHECK_FALSE(built.ok());
  DHEPZ_CHECK(built.Code() == core::ErrorCode::ParseError);
}

}  // namespace

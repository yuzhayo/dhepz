#include "modules/opencode/opencode_sessions.h"

#include <cstdio>
#include <ctime>
#include <string>
#include <utility>

#include "core/json.h"

namespace modules::opencode {
namespace {

// A millisecond epoch exceeds what a double represents exactly, and the JSON
// serializer prints doubles from their numeric value, so every integer goes
// through the raw-text form instead: the digits are kept verbatim.
json::Value Integer(long long value) {
  return json::Value::Number(std::to_wstring(value), static_cast<double>(value));
}

// Epoch milliseconds to local "YYYY-MM-DD HH:MM:SS". Empty for nonsense input;
// a session with no usable timestamp still exports, just with a blank cell.
std::wstring FormatTimeMs(long long milliseconds) {
  if (milliseconds <= 0) return {};
  const std::time_t seconds = static_cast<std::time_t>(milliseconds / 1000);
  std::tm local{};
  if (localtime_s(&local, &seconds) != 0) return {};
  wchar_t buffer[20];
  swprintf_s(buffer, L"%04d-%02d-%02d %02d:%02d:%02d", local.tm_year + 1900,
             local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min,
             local.tm_sec);
  return buffer;
}

}  // namespace

core::Status BuildSessionsDocument(std::wstring_view api_response,
                                   std::wstring_view generated,
                                   std::wstring* out_text,
                                   std::size_t* out_total) {
  if (out_text == nullptr || out_total == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::InvalidArgument,
                     L"session export requires both output parameters");
  }
  json::Value root;
  DHEPZ_RETURN_IF_ERROR(json::Parse(api_response, &root));
  const json::Value* data = root.ArrayField(L"data");
  if (data == nullptr) {
    return DHEPZ_ERR(core::ErrorCode::ParseError,
                     L"opencode api response has no session list");
  }

  json::Value sessions = json::Value::Array();
  for (const json::Value& item : data->items()) {
    if (!item.is_object()) continue;
    const json::Value* location = item.ObjectField(L"location");
    const json::Value* time = item.ObjectField(L"time");
    const long long created_ms =
        time != nullptr ? static_cast<long long>(time->NumberField(L"created")) : 0;
    const long long updated_ms =
        time != nullptr ? static_cast<long long>(time->NumberField(L"updated")) : 0;
    json::Value session = json::Value::Object();
    session.Set(L"id", json::Value::String(item.StringField(L"id")));
    session.Set(L"title", json::Value::String(item.StringField(L"title")));
    session.Set(L"directory",
                json::Value::String(location != nullptr
                                        ? location->StringField(L"directory")
                                        : std::wstring{}));
    session.Set(L"project", json::Value::String(item.StringField(L"projectID")));
    session.Set(L"agent", json::Value::String(item.StringField(L"agent")));
    session.Set(L"time_created", json::Value::String(FormatTimeMs(created_ms)));
    session.Set(L"time_updated", json::Value::String(FormatTimeMs(updated_ms)));
    session.Set(L"time_updated_ms", Integer(updated_ms));
    sessions.Append(std::move(session));
  }

  const std::size_t total = sessions.size();
  json::Value document = json::Value::Object();
  document.Set(L"generated", json::Value::String(std::wstring(generated)));
  document.Set(L"source", json::Value::String(L"opencode api get /api/session"));
  document.Set(L"total", Integer(static_cast<long long>(total)));
  document.Set(L"sessions", std::move(sessions));

  *out_total = total;
  *out_text = json::Serialize(document);
  return core::Ok();
}

}  // namespace modules::opencode

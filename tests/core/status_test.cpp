#include "core/status.h"

#include <string>

#include "framework/test_case.h"

DHEPZ_TEST(Status, SuccessIsEmptyAndTrue) {
  const core::Status status = core::Ok();
  DHEPZ_CHECK(status.ok());
  DHEPZ_CHECK(static_cast<bool>(status));
  DHEPZ_CHECK_EQ(status.Code(), core::ErrorCode::Ok);
  DHEPZ_CHECK(status.Message().empty());
}

DHEPZ_TEST(Status, DefaultConstructedIsSuccess) {
  const core::Status status;
  DHEPZ_CHECK(status.ok());
}

DHEPZ_TEST(Status, ErrorCarriesCodeAndMessage) {
  const core::Status status = core::Err(core::ErrorCode::NotFound, L"no screen named home");
  DHEPZ_CHECK_FALSE(status.ok());
  DHEPZ_CHECK_FALSE(static_cast<bool>(status));
  DHEPZ_CHECK_EQ(status.Code(), core::ErrorCode::NotFound);
  DHEPZ_CHECK_EQ(status.Message(), std::wstring(L"no screen named home"));
}

DHEPZ_TEST(Status, MacroStampsFileAndLine) {
  const core::Status status = DHEPZ_ERR(core::ErrorCode::ParseError, L"bad token");
  DHEPZ_CHECK_CONTAINS(status.Message(), std::wstring(L"bad token"));
  // Only the leaf of __FILEW__ is kept; the full path is machine-specific.
  DHEPZ_CHECK_CONTAINS(status.Message(), std::wstring(L"[status_test.cpp:"));
  DHEPZ_CHECK_FALSE(status.Message().find(L":\\") != std::wstring::npos);
}

DHEPZ_TEST(Status, ContextOnSuccessIsInert) {
  core::Status status = core::Ok();
  status.AddContext(L"somewhere\\else.cpp", 42);
  DHEPZ_CHECK(status.ok());
  DHEPZ_CHECK(status.Message().empty());
}

namespace {

core::Status Inner() { return DHEPZ_ERR(core::ErrorCode::PermissionDenied, L"registry write"); }

core::Status Outer() {
  DHEPZ_RETURN_IF_ERROR(Inner());
  return core::Ok();
}

int side_effect_calls = 0;

core::Status CountingOk() {
  ++side_effect_calls;
  return core::Ok();
}

}  // namespace

DHEPZ_TEST(Status, ReturnIfErrorPropagatesUnchanged) {
  const core::Status status = Outer();
  DHEPZ_CHECK_FALSE(status.ok());
  DHEPZ_CHECK_EQ(status.Code(), core::ErrorCode::PermissionDenied);
  // The context is the *inner* frame's line, not the propagating one, which is
  // the whole point of carrying it rather than re-stamping.
  DHEPZ_CHECK_CONTAINS(status.Message(), std::wstring(L"registry write"));
  DHEPZ_CHECK_CONTAINS(status.Message(), std::wstring(L"[status_test.cpp:"));
}

DHEPZ_TEST(Status, ReturnIfErrorEvaluatesOnce) {
  side_effect_calls = 0;
  const core::Status status = [] {
    DHEPZ_RETURN_IF_ERROR(CountingOk());
    return core::Ok();
  }();
  DHEPZ_CHECK(status.ok());
  DHEPZ_CHECK_EQ(side_effect_calls, 1);
}

DHEPZ_TEST(Status, CodeNamesAreStable) {
  DHEPZ_CHECK_EQ(std::wstring_view(core::ErrorCodeName(core::ErrorCode::Ok)), L"Ok");
  DHEPZ_CHECK_EQ(std::wstring_view(core::ErrorCodeName(core::ErrorCode::InvalidArgument)),
                 L"InvalidArgument");
  DHEPZ_CHECK_EQ(std::wstring_view(core::ErrorCodeName(core::ErrorCode::Internal)), L"Internal");
  // Out-of-range values reach a real caller only via a cast, but the switch must
  // not fall off the end.
  DHEPZ_CHECK_EQ(std::wstring_view(core::ErrorCodeName(static_cast<core::ErrorCode>(9999))),
                 L"Unknown");
}

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "core/status.h"

namespace modules::opencode {

// Projects the `opencode api get /api/session` response — one JSON object
// whose "data" array carries the Session.Info records — into the export
// document the tab writes to disk:
//
//   { "generated": ..., "source": ..., "total": N, "sessions": [ ... ] }
//
// `generated` is injected rather than read from a clock so the projection is
// pure and its exact output is testable. Fails with ParseError when the text
// is not JSON, or is JSON without a "data" array — the shape opencode
// promises today but a future version could rename, and that must surface as
// an error rather than a silently empty export.
core::Status BuildSessionsDocument(std::wstring_view api_response,
                                   std::wstring_view generated,
                                   std::wstring* out_text,
                                   std::size_t* out_total);

}  // namespace modules::opencode

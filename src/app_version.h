#pragma once

// Numeric version parts arrive as preprocessor defines from version.props via
// the project file. The string forms come from a generated header, because
// MSBuild cannot pass a quoted string define through to rc.exe intact and both
// compilers must read the same values.

#include "dhepz_version_generated.h"

namespace dhepz::version {

inline constexpr int kMajor = DHEPZ_VERSION_MAJOR;
inline constexpr int kMinor = DHEPZ_VERSION_MINOR;
inline constexpr int kPatch = DHEPZ_VERSION_PATCH;
inline constexpr int kBuild = DHEPZ_VERSION_BUILD;

inline constexpr const wchar_t* kString = DHEPZ_VERSION_STRING_W;

}  // namespace dhepz::version

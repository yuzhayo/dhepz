// Test registration and assertions.
//
// Self-registering, for the same reason module registration is: there is no
// central list of tests to edit. A new `.cpp` under `tests/` is picked up by the
// project's wildcard glob, and its DHEPZ_TEST blocks appear in the next run.
// Adding a test never touches a shared file, so two people adding tests never
// conflict (G5's principle, applied to the suite).
//
// This works only because tests compile directly into the test EXE. If they were
// ever put in a static .lib the linker would discard the unreferenced
// registration objects and the tests would silently vanish — the same trap the
// module objects have.
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace testing {

struct TestCase {
  std::string name;
  std::string file;
  int line = 0;
  std::function<void()> run;
};

// The registry is a function-local static rather than a namespace-scope one.
// Registration happens during dynamic initialisation, and a namespace-scope
// vector in another translation unit might not be constructed yet — the classic
// static initialisation order problem. A function-local static is constructed on
// first use, which is by definition before the first registration reads it.
std::vector<TestCase>& Registry();

struct Registrar {
  Registrar(std::string name, std::string file, int line, std::function<void()> run) {
    Registry().push_back(TestCase{std::move(name), std::move(file), line, std::move(run)});
  }
};

// Thrown by a failing assertion. Caught per test by the runner, so one failure
// does not end the run.
class AssertionFailure {
 public:
  AssertionFailure(std::string message, std::string file, int line)
      : message_(std::move(message)), file_(std::move(file)), line_(line) {}

  const std::string& message() const { return message_; }
  const std::string& file() const { return file_; }
  int line() const { return line_; }

 private:
  std::string message_;
  std::string file_;
  int line_ = 0;
};

// Renders a value for a failure message. Overloads rather than a stream
// template, so an unprintable type is a compile error naming the type instead of
// a wall of operator<< diagnostics.
std::string Describe(bool value);
std::string Describe(int value);
std::string Describe(long long value);
std::string Describe(unsigned long long value);
std::string Describe(double value);
std::string Describe(const char* value);
std::string Describe(const wchar_t* value);
std::string Describe(std::string_view value);
std::string Describe(std::wstring_view value);

// Enums print as their underlying integer. The framework deliberately does not
// know how to name a project enum — that would couple it to core/ — so a failure
// on an ErrorCode reads as a number. Assert on ErrorCodeName() when the name
// matters.
template <typename T>
  requires std::is_enum_v<T>
std::string Describe(T value) {
  return "enum(" + Describe(static_cast<long long>(static_cast<std::underlying_type_t<T>>(value))) +
         ")";
}

namespace detail {

// Comparing two raw string pointers compares addresses, not text. It usually
// even appears to work, because the compiler pools identical literals within one
// translation unit — so the test passes until the literal moves. DHEPZ_CHECK_EQ
// rejects it at compile time and tells the author to wrap in a string_view.
template <typename T>
constexpr bool kIsRawString = [] {
  using Bare = std::remove_cvref_t<T>;
  if constexpr (std::is_pointer_v<Bare>) {
    using Pointee = std::remove_cv_t<std::remove_pointer_t<Bare>>;
    return std::is_same_v<Pointee, char> || std::is_same_v<Pointee, wchar_t>;
  } else if constexpr (std::is_array_v<Bare>) {
    using Element = std::remove_cv_t<std::remove_extent_t<Bare>>;
    return std::is_same_v<Element, char> || std::is_same_v<Element, wchar_t>;
  } else {
    return false;
  }
}();

}  // namespace detail

[[noreturn]] void Fail(std::string message, const char* file, int line);

// DHEPZ_TEST(Suite, Name) { ... }
//
// Suite and name are pasted into `Suite.Name`, which is what --filter matches
// and what becomes the JUnit classname/name pair.
#define DHEPZ_TEST(suite, name)                                            \
  static void DhepzTest_##suite##_##name();                                \
  namespace {                                                              \
  const ::testing::Registrar dhepz_registrar_##suite##_##name(             \
      #suite "." #name, __FILE__, __LINE__, DhepzTest_##suite##_##name);   \
  }                                                                        \
  static void DhepzTest_##suite##_##name()

#define DHEPZ_CHECK(expr)                                                  \
  do {                                                                     \
    if (!(expr)) {                                                         \
      ::testing::Fail("expected true: " #expr, __FILE__, __LINE__);        \
    }                                                                      \
  } while (false)

#define DHEPZ_CHECK_FALSE(expr)                                            \
  do {                                                                     \
    if ((expr)) {                                                          \
      ::testing::Fail("expected false: " #expr, __FILE__, __LINE__);       \
    }                                                                      \
  } while (false)

// Each side is evaluated once into a named local, so an assertion on a function
// call does not call it twice and does not hide a side effect.
#define DHEPZ_CHECK_EQ(actual, expected)                                   \
  do {                                                                     \
    auto&& dhepz_actual__ = (actual);                                      \
    auto&& dhepz_expected__ = (expected);                                  \
    static_assert(!(::testing::detail::kIsRawString<decltype(actual)> &&   \
                    ::testing::detail::kIsRawString<decltype(expected)>),  \
                  "comparing two raw strings compares addresses, not text; " \
                  "wrap one side in std::string_view or std::wstring_view"); \
    if (!(dhepz_actual__ == dhepz_expected__)) {                           \
      ::testing::Fail(                                                     \
          std::string(#actual " == " #expected "\n    expected: ") +       \
              ::testing::Describe(dhepz_expected__) +                      \
              "\n    actual:   " + ::testing::Describe(dhepz_actual__),    \
          __FILE__, __LINE__);                                             \
    }                                                                      \
  } while (false)

#define DHEPZ_CHECK_NE(actual, unexpected)                                 \
  do {                                                                     \
    auto&& dhepz_actual__ = (actual);                                      \
    auto&& dhepz_unexpected__ = (unexpected);                              \
    if ((dhepz_actual__ == dhepz_unexpected__)) {                          \
      ::testing::Fail(                                                     \
          std::string(#actual " != " #unexpected "\n    both were: ") +    \
              ::testing::Describe(dhepz_actual__),                         \
          __FILE__, __LINE__);                                             \
    }                                                                      \
  } while (false)

// Substring containment, which is what most message assertions want. Exact
// wording is free to change; that a diagnostic mentions the offending name is
// the part worth asserting.
#define DHEPZ_CHECK_CONTAINS(haystack, needle)                             \
  do {                                                                     \
    auto&& dhepz_haystack__ = (haystack);                                  \
    auto&& dhepz_needle__ = (needle);                                      \
    using DhepzHaystack__ = std::remove_cvref_t<decltype(dhepz_haystack__)>; \
    if (dhepz_haystack__.find(dhepz_needle__) == DhepzHaystack__::npos) {  \
      ::testing::Fail(                                                     \
          std::string(#haystack " should contain " #needle                 \
                                "\n    needle:   ") +                      \
              ::testing::Describe(dhepz_needle__) +                        \
              "\n    haystack: " + ::testing::Describe(dhepz_haystack__),  \
          __FILE__, __LINE__);                                             \
    }                                                                      \
  } while (false)

#define DHEPZ_FAIL(message) ::testing::Fail((message), __FILE__, __LINE__)

}  // namespace testing

#include "framework/test_case.h"

// Temporary: proves CI turns a JUnit failure into an inline annotation.
DHEPZ_TEST(CiProbe, DeliberateFailure) {
  DHEPZ_CHECK_EQ(1, 2);
}
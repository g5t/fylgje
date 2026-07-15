// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief Tests for kafka::time duration string parsing
//===----------------------------------------------------------------------===//
#include <gtest/gtest.h>
#include "Time.h"

namespace {

using kafka::time::duration_string_to_milliseconds;

TEST(DurationStringTest, UnitSuffixes) {
  EXPECT_EQ(duration_string_to_milliseconds("60s").count(), 60'000);
  EXPECT_EQ(duration_string_to_milliseconds("5m").count(), 300'000);
  EXPECT_EQ(duration_string_to_milliseconds("1h").count(), 3'600'000);
  EXPECT_EQ(duration_string_to_milliseconds("2d").count(), 172'800'000);
  EXPECT_EQ(duration_string_to_milliseconds("500ms").count(), 500);
}

TEST(DurationStringTest, BareNumberMeansSeconds) {
  EXPECT_EQ(duration_string_to_milliseconds("60").count(), 60'000);
  EXPECT_EQ(duration_string_to_milliseconds("1").count(), 1'000);
}

TEST(DurationStringTest, DisabledAndInvalid) {
  EXPECT_EQ(duration_string_to_milliseconds("0").count(), 0);
  EXPECT_EQ(duration_string_to_milliseconds("0s").count(), 0);
  EXPECT_EQ(duration_string_to_milliseconds("").count(), 0);
  EXPECT_EQ(duration_string_to_milliseconds("abc").count(), 0); // must not throw
}

}

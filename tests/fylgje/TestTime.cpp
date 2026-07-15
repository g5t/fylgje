// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief Tests for kafka::time duration string parsing and formatting
//===----------------------------------------------------------------------===//
#include <gtest/gtest.h>
#include "Time.h"

namespace {

using kafka::time::duration_string_to_milliseconds;
using kafka::time::milliseconds_to_duration_string;

kafka::time::milliseconds ms(int64_t count) { return kafka::time::milliseconds{count}; }

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

TEST(DurationFormatTest, SubSecond) {
  EXPECT_EQ(milliseconds_to_duration_string(ms(0)), "0s");
  EXPECT_EQ(milliseconds_to_duration_string(ms(1)), "1ms");
  EXPECT_EQ(milliseconds_to_duration_string(ms(503)), "503ms");
}

TEST(DurationFormatTest, Seconds) {
  EXPECT_EQ(milliseconds_to_duration_string(ms(3'000)), "3s");
  EXPECT_EQ(milliseconds_to_duration_string(ms(3'400)), "3.4s");
  EXPECT_EQ(milliseconds_to_duration_string(ms(59'949)), "59.9s");
}

TEST(DurationFormatTest, MinutesAndHours) {
  EXPECT_EQ(milliseconds_to_duration_string(ms(60'000)), "1m00s");
  EXPECT_EQ(milliseconds_to_duration_string(ms(723'000)), "12m03s");
  EXPECT_EQ(milliseconds_to_duration_string(ms(723'456)), "12m03s"); // sub-second dropped above a minute
  EXPECT_EQ(milliseconds_to_duration_string(ms(3'723'000)), "1h02m03s");
  EXPECT_EQ(milliseconds_to_duration_string(ms(90'000'000)), "25h00m00s"); // hours is the top unit
}

TEST(DurationFormatTest, Negative) {
  EXPECT_EQ(milliseconds_to_duration_string(ms(-3'000)), "-3s");
}

TEST(DurationFormatTest, RoundTripSingleUnit) {
  for (const std::string repr: {"0s", "500ms", "30s"}) {
    EXPECT_EQ(milliseconds_to_duration_string(duration_string_to_milliseconds(repr)), repr);
  }
}

}

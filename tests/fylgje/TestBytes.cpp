// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief Tests for byte-count string parsing and formatting
//===----------------------------------------------------------------------===//
#include <gtest/gtest.h>
#include "Bytes.h"

namespace {

using fylgje::size_string_to_bytes;
using fylgje::bytes_to_size_string;

TEST(SizeStringTest, BareNumberMeansBytes) {
  EXPECT_EQ(size_string_to_bytes("0"), 0);
  EXPECT_EQ(size_string_to_bytes("1024"), 1024);
  EXPECT_EQ(size_string_to_bytes("65536"), 65536);
}

TEST(SizeStringTest, Suffixes) {
  EXPECT_EQ(size_string_to_bytes("1K"), 1024);
  EXPECT_EQ(size_string_to_bytes("64K"), 65536);
  EXPECT_EQ(size_string_to_bytes("512M"), int64_t{512} << 20);
  EXPECT_EQ(size_string_to_bytes("2G"), int64_t{2} << 30);
  EXPECT_EQ(size_string_to_bytes("1T"), int64_t{1} << 40);
}

TEST(SizeStringTest, CaseAndTrailingB) {
  EXPECT_EQ(size_string_to_bytes("1k"), 1024);
  EXPECT_EQ(size_string_to_bytes("512MB"), int64_t{512} << 20);
  EXPECT_EQ(size_string_to_bytes("512mb"), int64_t{512} << 20);
  EXPECT_EQ(size_string_to_bytes("512B"), 512);
}

TEST(SizeStringTest, Invalid) {
  EXPECT_EQ(size_string_to_bytes(""), 0);
  EXPECT_EQ(size_string_to_bytes("abc"), 0);   // must not throw
  EXPECT_EQ(size_string_to_bytes("-5M"), 0);   // negative sizes are meaningless
  EXPECT_EQ(size_string_to_bytes("12X"), 0);   // unknown suffix
}

TEST(SizeFormatTest, Formatting) {
  EXPECT_EQ(bytes_to_size_string(0), "0B");
  EXPECT_EQ(bytes_to_size_string(512), "512B");
  EXPECT_EQ(bytes_to_size_string(1536), "1.5K");
  EXPECT_EQ(bytes_to_size_string(int64_t{512} << 20), "512M");
  EXPECT_EQ(bytes_to_size_string(int64_t{2} << 30), "2G");
}

TEST(SizeFormatTest, RoundTrip) {
  for (const std::string repr: {"64K", "512M", "1G"}) {
    EXPECT_EQ(bytes_to_size_string(size_string_to_bytes(repr)), repr);
  }
}

}

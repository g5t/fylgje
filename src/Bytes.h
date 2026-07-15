// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief Byte-count string parsing and formatting, e.g. "512M" <-> 536870912
//===----------------------------------------------------------------------===//
#pragma once
#include <cstdint>
#include <string>

namespace fylgje {
  /// \brief parse a byte count: a bare number means bytes; K/M/G/T suffixes
  ///        are 1024-based and case-insensitive, with an optional trailing
  ///        'B' (512M == 512MB == 536870912). Invalid strings yield 0.
  int64_t size_string_to_bytes(const std::string & size_str);

  /// \brief compact human-readable byte count: 512B, 1.5K, 512M, 2G
  std::string bytes_to_size_string(int64_t bytes);
}

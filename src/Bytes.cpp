// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief Byte-count string parsing and formatting, e.g. "512M" <-> 536870912
//===----------------------------------------------------------------------===//
#include <array>
#include <cctype>
#include <iostream>
#include <utility>
#include <fmt/format.h>

#include "Bytes.h"

int64_t fylgje::size_string_to_bytes(const std::string & size_str){
  if (size_str.empty()){
    return 0;
  }
  int64_t count{0};
  try {
    count = std::stoll(size_str);
  } catch (const std::exception &) {
    std::cout << "Unknown size string " << size_str << std::endl;
    return 0;
  }
  if (count < 0){
    std::cout << "Unknown size string " << size_str << std::endl;
    return 0;
  }
  auto str = size_str;
  if (str.back() == 'B' || str.back() == 'b'){
    str.pop_back();
  }
  if (str.empty() || std::isdigit(static_cast<unsigned char>(str.back()))){
    // no scale suffix: interpret the bare number as bytes
    return count;
  }
  switch (std::toupper(static_cast<unsigned char>(str.back()))) {
    case 'K': return count << 10;
    case 'M': return count << 20;
    case 'G': return count << 30;
    case 'T': return count << 40;
    default: break;
  }
  std::cout << "Unknown size string " << size_str << std::endl;
  return 0;
}

std::string fylgje::bytes_to_size_string(int64_t bytes){
  std::string sign;
  if (bytes < 0){
    sign = "-";
    bytes = -bytes;
  }
  constexpr std::array<std::pair<int64_t, char>, 4> scales{{
      {int64_t{1} << 40, 'T'}, {int64_t{1} << 30, 'G'}, {int64_t{1} << 20, 'M'}, {int64_t{1} << 10, 'K'}}};
  for (const auto & [scale, suffix] : scales){
    if (bytes >= scale){
      const auto whole = bytes / scale;
      const auto tenths = (bytes % scale) * 10 / scale;
      return tenths ? fmt::format("{}{}.{}{}", sign, whole, tenths, suffix)
                    : fmt::format("{}{}{}", sign, whole, suffix);
    }
  }
  return fmt::format("{}{}B", sign, bytes);
}

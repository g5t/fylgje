// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief Utilities to help convert times to Kafka's standard: milliseconds since epoch
//===----------------------------------------------------------------------===//
#pragma once
#include <string>
#include <ctime>
#include <chrono>

namespace kafka::time {
  std::time_t now_to_time_t();
  std::time_t string_to_time_t(const std::string & time_str);

  // std::string time_t_to_string(std::time_t time);

  using milliseconds = std::chrono::duration<int64_t, std::milli>;

  /// \brief current wall-clock time, same clock domain as Kafka message timestamps
  milliseconds now_milliseconds();

  milliseconds time_t_to_milliseconds(std::time_t time);
  milliseconds time_string_to_milliseconds(const std::string & time_str);
  milliseconds duration_string_to_milliseconds(const std::string & duration_str);

  /// \brief compact human-readable duration: 503ms, 3.4s, 12m03s, 1h02m03s
  ///        (hours is the top unit; sub-second detail dropped above a minute)
  std::string milliseconds_to_duration_string(milliseconds duration);
}


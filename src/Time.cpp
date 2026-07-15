// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief Utilities to help convert times to Kafka's standard: milliseconds since epoch
//===----------------------------------------------------------------------===//
#include <cctype>
#include <iostream>
#include <iomanip>
#include <chrono>
#include <fmt/format.h>

#include "Time.h"

using namespace kafka::time;

std::time_t kafka::time::now_to_time_t() {
  return std::time(nullptr);
}

milliseconds kafka::time::now_milliseconds() {
  return std::chrono::duration_cast<milliseconds>(std::chrono::system_clock::now().time_since_epoch());
}

std::time_t kafka::time::string_to_time_t(const std::string & time_str) {
  struct std::tm tm{};
  std::istringstream ss(time_str);
  ss >> std::get_time(&tm, "%Y-%m-%dT%TZ");
  if (ss.fail()){
    throw std::runtime_error(fmt::format("Failed to parse UTC time from '{}'", time_str));
  }
  auto time = timegm(&tm);
  if (ss.peek() == '.') {
    ss.ignore();
    double fractional;
    ss >> fractional;
    if (ss.fail()) {
      std::cout << fmt::format("Failed to parse fractional seconds from '{}'", time_str) << std::endl;
    } else {
      auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::duration<double>(fractional));
      auto in_seconds = std::chrono::duration_cast<std::chrono::seconds>(microseconds);
      time += std::chrono::duration_cast<std::chrono::seconds>(microseconds).count();
    }
  }
  return time;
}

std::string kafka::time::time_t_to_string(std::time_t time) {
  char timeString[std::size("yyyy-mm-ddThh:mm:ssZ")];
  std::strftime(std::data(timeString), std::size(timeString), "%FT%TZ", std::gmtime(&time));
  return timeString;
}

milliseconds kafka::time::time_t_to_milliseconds(std::time_t time) {
  return std::chrono::duration<int64_t, std::milli>(time * 1000);
}

milliseconds kafka::time::time_string_to_milliseconds(const std::string & time_str){
  return time_t_to_milliseconds(string_to_time_t(time_str));
}

milliseconds kafka::time::duration_string_to_milliseconds(const std::string & duration_str){
  if (duration_str.empty()){
    return std::chrono::milliseconds(0);
  }
  int count{0};
  try {
    count = std::stoi(duration_str);
  } catch (const std::exception &) {
    std::cout << "Unknown duration string " << duration_str << std::endl;
    return std::chrono::milliseconds(0);
  }
  if (std::isdigit(static_cast<unsigned char>(duration_str.back()))){
    // no unit suffix: interpret the bare number as seconds
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::seconds(count));
  }
  if (duration_str.back() == 'd'){
    auto days = std::chrono::hours(24 * count);
    return std::chrono::duration_cast<std::chrono::milliseconds>(days);
  }
  if (duration_str.back() == 'h'){
    auto hours = std::chrono::hours(count);
    return std::chrono::duration_cast<std::chrono::milliseconds>(hours);
  }
  if (duration_str.back() == 'm'){
    auto minutes = std::chrono::minutes(count);
    return std::chrono::duration_cast<std::chrono::milliseconds>(minutes);
  }
  if (duration_str.back() == 's'){
    // the character before the trailing 's' distinguishes ms/us/ns from plain seconds
    auto prev = duration_str.size() > 1 ? *(std::end(duration_str)-2) : '\0';
    if (prev == 'm'){
      auto milliseconds = std::chrono::milliseconds(count);
      return std::chrono::duration_cast<std::chrono::milliseconds>(milliseconds);
    }
    if (prev == 'u'){
      auto microseconds = std::chrono::microseconds(count);
      return std::chrono::duration_cast<std::chrono::milliseconds>(microseconds);
    }
    if (prev == 'n'){
      auto nanoseconds = std::chrono::nanoseconds(count);
      return std::chrono::duration_cast<std::chrono::milliseconds>(nanoseconds);
    }
    auto seconds = std::chrono::seconds(count);
    return std::chrono::duration_cast<std::chrono::milliseconds>(seconds);
  }
  std::cout << "Unknown duration string " << duration_str << std::endl;
  return std::chrono::milliseconds(0);
}
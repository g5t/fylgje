// Copyright (C) 2023 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file ESSConsumer.cpp
///
//===----------------------------------------------------------------------===//

#include "ESSConsumer.h"
#include <fmt/format.h>
#include <tuple>


/**
 * @brief Convert packet header and data times to seconds since reference time
 * @param pulse_hi Latest header reference time integer seconds since epoch
 * @param pulse_lo Latest header reference time 88.053 MHz ticks since pulse_hi
 * @param prev_hi Previous header reference time integer seconds since epoch
 * @param prev_lo Previous header reference time 88.053 MHz ticks since prev_hi
 * @param high Event reference time integer seconds since epoch
 * @param low Event reference time 88.053 MHz ticks since high
 * @return A positive double representing the time in seconds since _a_ reference time
 */
std::tuple<double, uint32_t, uint32_t> frame_time(uint32_t pulse_hi, uint32_t pulse_lo, uint32_t prev_hi, uint32_t prev_lo, uint32_t high, uint32_t low){
  auto converter = [high,low](uint32_t h, uint32_t l) {
    // low is allowed to be less than l, in which case direct subtraction would yield a large positive integer
    // if the cast to int is not done before subtraction.
    const int ticks = 88'052'500;
    auto diff = static_cast<int>(low)-static_cast<int>(l);
    return static_cast<double>(high-h) + static_cast<double>(diff) / ticks;
  };
  double time{0.};
  uint32_t p_hi, p_lo;
  if (high > pulse_hi || (high == pulse_hi && low > pulse_lo)){
    time =  converter(pulse_hi, pulse_lo);
    p_hi = pulse_hi;
    p_lo = pulse_lo;
  } else if (high > prev_hi || (high == prev_hi && low > prev_lo)){
    time = converter(prev_hi, prev_lo);
    p_hi = prev_hi;
    p_lo = prev_lo;
  }
  return {time, p_hi, p_lo};
}

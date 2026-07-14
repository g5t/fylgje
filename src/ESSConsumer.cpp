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
 * @param header Header reference times for this and the previous ticks
 * @param high Event reference time integer seconds since epoch
 * @param low Event reference time 88.053 MHz ticks since high
 * @return A positive double representing the time in seconds since _a_ reference time
 */
std::tuple<double, uint32_t, uint32_t> frame_time(const ess::network::PulseTimes * header, uint32_t high, uint32_t low){
  auto converter = [high,low](uint32_t h, uint32_t l) {
    // low is allowed to be less than l, in which case direct subtraction would yield a large positive integer
    // if the cast to int is not done before subtraction.
    const int ticks = 88'052'500;
    auto diff = static_cast<int>(low)-static_cast<int>(l);
    return static_cast<double>(high-h) + static_cast<double>(diff) / ticks;
  };
  double time{0.};
  uint32_t p_hi, p_lo;
  if (high > header->PulseHigh || (high == header->PulseHigh && low > header->PulseLow)){
    time =  converter(header->PulseHigh, header->PulseLow);
    p_hi = header->PulseHigh;
    p_lo = header->PulseLow;
  } else if (high > header->PrevPulseHigh || (high == header->PrevPulseHigh && low > header->PrevPulseLow)){
    time = converter(header->PrevPulseHigh, header->PrevPulseLow);
    p_hi = header->PrevPulseHigh;
    p_lo = header->PrevPulseLow;
  } else {
    // Hopefully impossible case
    p_hi = 0;
    p_lo = 0;
  }
  return {time, p_hi, p_lo};
}

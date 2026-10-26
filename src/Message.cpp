// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief details needed for fylgje BIFROST to write-out AR51 message events to HDF5
//===----------------------------------------------------------------------===//
#include "Message.h"

HighFive::CompoundType bifrost::message_type() {
  using HighFive::create_datatype;
  // explicit offsets and size: the struct layout, not HighFive's packing rules
  return {{
      {"fiber", create_datatype<int>(), offsetof(bifrost::message_t, fiber)},
      {"group", create_datatype<int>(), offsetof(bifrost::message_t, group)},
      {"a", create_datatype<int>(), offsetof(bifrost::message_t, a)},
      {"b", create_datatype<int>(), offsetof(bifrost::message_t, b)},
      {"time", create_datatype<double>(), offsetof(bifrost::message_t, time)},
      {"high", create_datatype<uint32_t>(), offsetof(bifrost::message_t, high)},
      {"low", create_datatype<uint32_t>(), offsetof(bifrost::message_t, low)},
  }, sizeof(bifrost::message_t)};
}

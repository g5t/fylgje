// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief details needed for fylgje BIFROST to write-out AR51 message events to HDF5
//===----------------------------------------------------------------------===//
#include "Message.h"

hdf5::datatype::Compound bifrost::message_type() {
  auto compound = hdf5::datatype::Compound::create(sizeof(bifrost::message_t));
  compound.insert("fiber", offsetof(bifrost::message_t, fiber), hdf5::datatype::create<int>());
  compound.insert("group", offsetof(bifrost::message_t, group), hdf5::datatype::create<int>());
  compound.insert("a", offsetof(bifrost::message_t, a), hdf5::datatype::create<int>());
  compound.insert("b", offsetof(bifrost::message_t, b), hdf5::datatype::create<int>());
  compound.insert("time", offsetof(bifrost::message_t, time), hdf5::datatype::create<double>());
  compound.insert("high", offsetof(bifrost::message_t, high), hdf5::datatype::create<uint32_t>());
  compound.insert("low", offsetof(bifrost::message_t, low), hdf5::datatype::create<uint32_t>());
  return compound;
}

// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief Deterministic synthetic ar51/ESS-packet generation for consumer tests
//===----------------------------------------------------------------------===//
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace fylgje::test {

struct SyntheticMessage {
  int32_t partition;
  int64_t timestamp_ms;
  std::vector<uint8_t> payload; ///< finished ar51 flatbuffer
};

/// \brief One ESS data packet: PacketHeaderV0/V1 followed by CAEN readouts
/// \param version 0 or 1 (V1 appends the two CMACPadd bytes to the header)
/// \param n_readouts number of CAENReadout structs following the header
/// \param pulse_high header pulse time, integer seconds part
/// \param pulse_low header pulse time, tick part; readouts are placed just after it
/// \param seq_num header sequence number, also seeds the readout fields
std::vector<uint8_t> make_caen_packet(uint8_t version, int n_readouts,
                                      uint32_t pulse_high, uint32_t pulse_low,
                                      uint32_t seq_num);

/// \brief Wrap an ESS packet in a finished ar51 RawReadoutMessage flatbuffer
std::vector<uint8_t> make_ar51_message(const std::string & source,
                                       int64_t message_id,
                                       const std::vector<uint8_t> & packet);

/// \brief A reproducible plan for a stream of messages, round-robin across
///        partitions with evenly spaced timestamps, so tests can compute the
///        counts a consumer should see for any time window
struct SyntheticStream {
  int n_messages{30};
  int n_partitions{3};
  int readouts_per_message{10};
  int64_t t0_ms{1'700'000'000'000};
  int64_t dt_ms{1'000};

  [[nodiscard]] std::vector<SyntheticMessage> messages(const std::string & source = "bifrost") const;

  /// \brief messages a window-limited consumer should see: from <= timestamp < to
  [[nodiscard]] int count_in_window(int64_t from_ms, int64_t to_ms) const;

  [[nodiscard]] int64_t end_ms() const { return t0_ms + n_messages * dt_ms; }
};

}

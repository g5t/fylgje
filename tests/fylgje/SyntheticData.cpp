// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief Deterministic synthetic ar51/ESS-packet generation for consumer tests
//===----------------------------------------------------------------------===//
#include "SyntheticData.h"
#include "Network.h"
#include "ar51_readout_data_generated.h"
#include <cstring>

namespace fylgje::test {

std::vector<uint8_t> make_caen_packet(const uint8_t version, const int n_readouts,
                                      const uint32_t pulse_high, const uint32_t pulse_low,
                                      const uint32_t seq_num) {
  using ess::network::PacketHeaderV0;
  using ess::network::PacketHeaderV1;
  using ess::network::CAENReadout;
  const auto header_size = version == 1 ? sizeof(PacketHeaderV1) : sizeof(PacketHeaderV0);
  const auto total = header_size + n_readouts * sizeof(CAENReadout);

  std::vector<uint8_t> packet(total, 0);
  // V1 only adds trailing padding bytes, so a V0 header describes both layouts
  PacketHeaderV0 header{};
  header.Padding0 = 0;
  header.Version = version;
  header.CookieAndType = (3u << 28) | 0x535345; // "ESS" cookie, type 3 = CAEN
  header.TotalLength = static_cast<uint16_t>(total);
  header.OutputQueue = 0;
  header.TimeSource = 0;
  header.PulseHigh = pulse_high;
  header.PulseLow = pulse_low;
  header.PrevPulseHigh = pulse_high > 0 ? pulse_high - 1 : 0;
  header.PrevPulseLow = pulse_low;
  header.SeqNum = seq_num;
  std::memcpy(packet.data(), &header, sizeof(header));

  auto * readouts = packet.data() + header_size;
  for (int i = 0; i < n_readouts; ++i) {
    CAENReadout readout{};
    readout.Fiber = static_cast<uint8_t>((seq_num + i) % 10);
    readout.FEN = 0; // parseCAENData treats non-zero FEN as a bad readout (BIFROST)
    readout.Length = sizeof(CAENReadout);
    // strictly after the header pulse time, so frame_time takes the first branch
    readout.HighTime = pulse_high;
    readout.LowTime = pulse_low + 1 + static_cast<uint32_t>(i);
    readout.Flags_OM = 0;
    readout.Group = static_cast<uint8_t>((seq_num + i) % 15);
    readout.Unused = 0;
    readout.A = static_cast<int16_t>(100 + i);
    readout.B = static_cast<int16_t>(200 + i);
    readout.C = 0;
    readout.D = 0;
    std::memcpy(readouts + i * sizeof(CAENReadout), &readout, sizeof(readout));
  }
  return packet;
}

std::vector<uint8_t> make_ar51_message(const std::string & source,
                                       const int64_t message_id,
                                       const std::vector<uint8_t> & packet) {
  flatbuffers::FlatBufferBuilder builder(1024 + packet.size());
  const auto message = CreateRawReadoutMessageDirect(builder, source.c_str(), message_id, &packet);
  FinishRawReadoutMessageBuffer(builder, message);
  return {builder.GetBufferPointer(), builder.GetBufferPointer() + builder.GetSize()};
}

std::vector<SyntheticMessage> SyntheticStream::messages(const std::string & source) const {
  std::vector<SyntheticMessage> result;
  result.reserve(n_messages);
  for (int i = 0; i < n_messages; ++i) {
    const auto timestamp = t0_ms + i * dt_ms;
    const auto packet = make_caen_packet(0, readouts_per_message,
                                         static_cast<uint32_t>(timestamp / 1000),
                                         0, static_cast<uint32_t>(i));
    result.push_back({i % n_partitions, timestamp, make_ar51_message(source, i, packet)});
  }
  return result;
}

int SyntheticStream::count_in_window(const int64_t from_ms, const int64_t to_ms) const {
  int count{0};
  for (int i = 0; i < n_messages; ++i) {
    const auto timestamp = t0_ms + i * dt_ms;
    if (timestamp >= from_ms && timestamp < to_ms) ++count;
  }
  return count;
}

}

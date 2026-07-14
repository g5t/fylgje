// Copyright (C) 2023 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file ESSConsumer.h
///
/// \brief Wrapper class for librdkafka
///
/// Sets up the kafka consumer and handles binning of event pixel ids
//===----------------------------------------------------------------------===//

#pragma once

#include "Configuration.h"
#include "ar51_readout_data_generated.h"
#include <librdkafka/rdkafkacpp.h>
#include "EventManager.h"
#include "KafkaConfig.h"
#include "Time.h"
#include "Network.h"

std::tuple<double, uint32_t, uint32_t> frame_time(const ess::network::PulseTimes * header, uint32_t high, uint32_t low);


template<class T>
class ESSConsumer {
public:
  using kafka_config_t = std::vector<std::pair<std::string, std::string>>;
  using kafka_time_t = kafka::time::milliseconds;
  using data_t = T;
  using ptr_t = std::shared_ptr<T>;
  using Status = ess::network::Status;
  using Start = ess::network::Start;
private:
  Configuration & configuration;
  RdKafka::KafkaConsumer * mConsumer {nullptr};
  int64_t earliest_timestamp{-1}, latest_timestamp{-1};
  ptr_t histograms;
  /// \brief loadable Kafka-specific configuration
  kafka_config_t kafkaConfig;
  int64_t total_ar51{0};
  int64_t good_readout_count{0};
  int64_t bad_message_count{0};
  int64_t bad_readout_count{0};

  ess::network::CallbacksType callbacks{
    {3, std::bind(&ESSConsumer::parseCAENData, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)},
    {4, std::bind(&ESSConsumer::parseVMM3aData, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)},
    {6, std::bind(&ESSConsumer::parseCDTData, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)}
  };

public:
  /// \brief Construct a consumer of AR51 messages which seeks out a finite time window
  ///
  /// \param data The structure into which any found readout packets are inserted
  /// \param Config A combined Kafka and ESS configuration structure
  /// \param from The earliest time to look for messages in the Kafka topic stream
  /// \param to The latest time to look for messages in the Kafka topic stream
  ESSConsumer(ptr_t data, Configuration & Config, kafka::time::milliseconds from, kafka::time::milliseconds to):
  configuration{Config}, histograms{data} {
    setup();
    consumeFrom(from);
    consumeUntil(to);
  }

  /// \brief Construct a consumer of AR51 messages which collects messages from a specified time without end
  ///
  /// \param data The structure into which any found readout packets are inserted
  /// \param Config A combined Kafka and ESS configuration structure
  /// \param from The earliest time to look for messages in the Kafka topic stream
  ESSConsumer(ptr_t data, Configuration & Config, kafka::time::milliseconds from):
      configuration{Config}, histograms{data} {
    setup();
    consumeFrom(from);
  }

  /// \brief Construct a consumer of AR51 messages which collects messages from the current time without end
  ///
  /// \param data The structure into which any found readout packets are inserted
  /// \param Config A combined Kafka and ESS configuration structure
  ESSConsumer(ptr_t data, Configuration & Config):
      configuration{Config}, histograms{data} {
    using namespace kafka::time;
    setup();
    consumeFrom(time_t_to_milliseconds(now_to_time_t()));
  };


  /// \brief wrapper function for librdkafka consumer
  RdKafka::Message *consume() const { return mConsumer->consume(1000); }


  [[maybe_unused]] void consumeAll() const {
    ess::network::consume_all(configuration, mConsumer);
  }
  void consumeForever() {
    latest_timestamp = -1;
  }
  void consumeFrom(const std::optional<kafka_time_t> ms_since_utc_epoch){
    earliest_timestamp = ess::network::consume_from(configuration, mConsumer, ms_since_utc_epoch);
  }
  void consumeUntil(const std::optional<kafka_time_t> ms_since_utc_epoch){
    latest_timestamp = ess::network::consume_until(configuration, mConsumer, earliest_timestamp, ms_since_utc_epoch);
  }

  void run(){
    Status intent{Status::Continue};
    while (intent != Status::Halt) {
      const auto Msg = consume();
      auto [status, ar51s] = ess::network::handle_message(earliest_timestamp, latest_timestamp, Msg, callbacks);
      total_ar51 += ar51s;
      delete Msg;
      intent = status;
    }
    fmt::print("Processed {} AR51 messages ({} bad) and {} CAEN readouts ({} bad)\n", total_ar51, bad_message_count, good_readout_count, bad_readout_count);
  }

  /// \brief Parser for CAEN Data from BIFROST
  uint32_t parseCAENData(const uint8_t * Readout, const int Size, const ess::network::PulseTimes * header) {
    uint32_t processed{0};
    int BytesLeft = Size;
    constexpr auto bytes = sizeof(ess::network::CAENReadout);
    while (BytesLeft >= static_cast<int>(bytes)) {
      if (auto * crd = reinterpret_cast<const ess::network::CAENReadout *>(Readout); crd->FEN != 0){
        // FIXME: this is BIFROST specific; other instruments actually use their FEN counter -- remove or make flexible
        printf("FEN %u, Length %u, HighTime %u, LowTime %u, Flags %u, Group %u\n",
               crd->FEN, crd->Length, crd->HighTime, crd->LowTime, crd->Flags_OM, crd->Group);
        ++bad_readout_count;
      } else {
        auto [time, h, l] = frame_time(header, crd->HighTime, crd->LowTime);
        histograms->add(crd->Fiber, crd->Group, crd->A, crd->B, time, h, l);
      }
      BytesLeft -= bytes;
      Readout += bytes;
      ++processed;
    }
    if (BytesLeft) {
      // printf("CAEN readout message has %d extra bytes at the end\n", BytesLeft);
      ++bad_message_count;
    }

    good_readout_count += processed;
    return processed;
  }

  uint32_t parseVMM3aData(const uint8_t * Readout, const int Size, const ess::network::PulseTimes * header) {
    uint32_t processed{0};
    int BytesLeft = Size;
    constexpr auto bytes = sizeof(ess::network::VMM3aReadout);
    while (BytesLeft >= static_cast<int>(bytes)) {
      auto * vmm = reinterpret_cast<const ess::network::VMM3aReadout *>(Readout);
      auto [time, h, l] = frame_time(header, vmm->TimeHi, vmm->TimeLo);

      const auto ring = vmm->Fiber/2;
      const auto fen = vmm->FEN;
      const auto hybrid = vmm->VMM >> 1;
      const auto asic = vmm->VMM & 1;
      const auto channel = vmm->Channel;
      fmt::print("Ring {}, FEN {}, Hybrid {}, ASIC {}, Channel {}, Time {:.6f}, High {}, Low {}\n",
                 ring, fen, hybrid, asic, channel, time, h, l);

      BytesLeft -= bytes;
      Readout += bytes;
      ++processed;
    }
    if (BytesLeft) {
      // printf("VMM3a readout message has %d extra bytes at the end\n", BytesLeft);
      ++bad_message_count;
    }

    good_readout_count += processed;
    return processed;
  }
  uint32_t parseCDTData(const uint8_t * Readout, const int Size, const ess::network::PulseTimes * header) {
    uint32_t processed{0};
    int BytesLeft = Size;
    constexpr auto bytes = sizeof(ess::network::CDTReadout);
    while (BytesLeft >= static_cast<int>(bytes)) {
      auto * cdt = reinterpret_cast<const ess::network::CDTReadout *>(Readout);
      auto [time, h, l] = frame_time(header, cdt->TimeHi, cdt->TimeLo);

      const auto ring = cdt->Fiber/2;
      const auto fen = cdt->FEN;
      const auto cathode = cdt->Cathode;
      const auto anode = cdt->Anode;
      fmt::print("Ring {}, FEN {}, Cathode {}, Anode {}, Time {:.6f}, High {}, Low {}\n",
                 ring, fen, cathode, anode, time, h, l);

      BytesLeft -= bytes;
      Readout += bytes;
      ++processed;
    }
    if (BytesLeft) {
      // printf("CDT readout message has %d extra bytes at the end\n", BytesLeft);
      ++bad_message_count;
    }

    good_readout_count += processed;
    return processed;
  }


  [[nodiscard]] int64_t message_count() const {
    return total_ar51;
  }
  [[nodiscard]] int64_t event_count() const {
    return good_readout_count;
  }

private:
  void setup() {
    kafkaConfig = KafkaConfig(configuration.KafkaConfigFile).CfgParms;
    mConsumer = ess::network::subscribe_topic(configuration, kafkaConfig);
    assert(mConsumer != nullptr);
    //set_consumer_offset(Beginning, 0);
    set_consumer_offset(configuration, mConsumer, ess::network::Time, earliest_timestamp);
  }

};

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

std::tuple<double, uint32_t, uint32_t> frame_time(uint32_t pulse_hi, uint32_t pulse_lo, uint32_t prev_hi, uint32_t prev_lo, uint32_t high, uint32_t low);


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
  int32_t my_partition{0};
  int64_t earliest_timestamp{-1}, latest_timestamp{-1};
  ptr_t histograms;
  /// \brief loadable Kafka-specific configuration
  kafka_config_t kafkaConfig;
  int64_t total_ar51{0};
  int64_t total_caen{0};

  ess::network::DetectorCallbacksType callbacks{
    {3, std::bind(&ESSConsumer::parseCAENData, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6)}
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
  RdKafka::Message *consume() { return mConsumer->consume(1000); }


  [[maybe_unused]] void consumeAll() {
    ess::network::consume_all(configuration, my_partition, mConsumer);
  }
  void consumeForever() {
    latest_timestamp = -1;
  }
  void consumeFrom(std::optional<kafka_time_t> ms_since_utc_epoch){
    earliest_timestamp = ess::network::consume_from(configuration, my_partition, mConsumer, ms_since_utc_epoch);
  }
  void consumeUntil(std::optional<kafka_time_t> ms_since_utc_epoch){
    latest_timestamp = ess::network::consume_until(configuration, my_partition, mConsumer, earliest_timestamp, ms_since_utc_epoch);
  }

  void run(){
    Status intent{Status::Continue};
    while (intent != Status::Halt) {
      auto Msg = consume();
      auto [status, ar51s] = ess::network::handle_message(earliest_timestamp, latest_timestamp, Msg, callbacks);
      total_ar51 += ar51s;
      delete Msg;
      intent = status;
    }
    fmt::print("Processed {} AR51 messages and {} CAEN readouts\n", total_ar51, total_caen);
  }

  /// \brief Parser for CAEN Data from BIFROST
  uint32_t parseCAENData(uint8_t * Readout, int Size, uint32_t hi, uint32_t lo, uint32_t p_hi, uint32_t p_lo) {
    uint32_t processed{0};
    int BytesLeft = Size;
    const auto bytes = sizeof(ess::network::CAENReadout);
    while (BytesLeft >= static_cast<int>(bytes)) {
      auto * crd = (ess::network::CAENReadout *)Readout;
      if (crd->FEN != 0){
        printf("FEN %u, Length %u, HighTime %u, LowTime %u, Flags %u, Group %u\n",
               crd->FEN, crd->Length, crd->HighTime, crd->LowTime, crd->Flags_OM, crd->Group);
      } else {
        auto [time, h, l] = frame_time(hi, lo, p_hi, p_lo, crd->HighTime, crd->LowTime);
        histograms->add(crd->Fiber, crd->Group, crd->A, crd->B, time, h, l);
      }
      BytesLeft -= bytes;
      Readout += bytes;
      ++processed;
    }
    total_caen += processed;
    return processed;
  }

  [[nodiscard]] int64_t message_count() const {
    return total_ar51;
  }
  [[nodiscard]] int64_t event_count() const {
    return total_caen;
  }

private:
  void setup() {
    kafkaConfig = KafkaConfig(configuration.KafkaConfigFile).CfgParms;
    mConsumer = ess::network::subscribe_topic(configuration, kafkaConfig);
    assert(mConsumer != nullptr);
    //set_consumer_offset(Beginning, 0);
    my_partition = set_consumer_offset(configuration, mConsumer, ess::network::Time, earliest_timestamp);
  }

};

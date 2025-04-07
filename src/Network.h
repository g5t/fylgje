// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief Network data structures for the ESS readout system and interface details for Kafka service
//===----------------------------------------------------------------------===//
#pragma once
#include "Configuration.h"
#include "ar51_readout_data_generated.h"
#include <librdkafka/rdkafkacpp.h>
#include "EventManager.h"
#include "KafkaConfig.h"
#include "Time.h"

namespace ess::network {
  enum Status {Continue, Update, Halt};
  enum Start {Beginning, End, Time};

// Data format for the common ESS readout header
  struct PacketHeaderV0 {
    uint8_t Padding0;
    uint8_t Version;
    uint32_t CookieAndType;
    uint16_t TotalLength;
    uint8_t OutputQueue;
    uint8_t TimeSource;
    uint32_t PulseHigh;
    uint32_t PulseLow;
    uint32_t PrevPulseHigh;
    uint32_t PrevPulseLow;
    uint32_t SeqNum;
  } __attribute__((packed));

  struct CAENReadout {
    uint8_t Fiber;
    uint8_t FEN;
    uint16_t Length;
    uint32_t HighTime;
    uint32_t LowTime;
    uint8_t Flags_OM;
    uint8_t Group;
    uint16_t Unused;
    int16_t A;
    int16_t B;
    int16_t C;
    int16_t D;
  } __attribute__((packed));


/// \brief setup librdkafka parameters for Broker and Topic
  RdKafka::KafkaConsumer * subscribe_topic(Configuration & Config, const std::vector<std::pair<std::string, std::string>> & kafkaConfig);

  int64_t consume_all(Configuration & configuration, int32_t partition, RdKafka::KafkaConsumer * consumer);
  int64_t consume_from(Configuration & configuration, int32_t partition, RdKafka::KafkaConsumer * consumer, std::optional<kafka::time::milliseconds> since_epoch);
  int64_t consume_until(Configuration & configuration, int32_t partition, RdKafka::KafkaConsumer * consumer, int64_t early, std::optional<kafka::time::milliseconds> since_epoch);
//
//  void set_topic_partition_offset(
//      Configuration & configuration,
//      int32_t partition,
//      RdKafka::KafkaConsumer * consumer,
//      std::vector<RdKafka::TopicPartition*>& tps,
//      Start start,
//      int64_t ms_since_utc_epoch
//  );
//

  int32_t set_consumer_offset(
      Configuration & configuration,
      RdKafka::KafkaConsumer * consumer,
      Start start,
      int64_t ms_since_utc_epoch
  );

  using DetectorTypeCallback = std::function<uint32_t(uint8_t*, int, uint32_t, uint32_t, uint32_t, uint32_t)>;
  using DetectorCallbacksType = std::map<uint32_t, DetectorTypeCallback>;

/// \brief initial checks for kafka error messages
/// \return Update if data is processed, Continue if no data, or Halt if finished or errored
  std::tuple<Status, uint32_t> handle_message(int64_t early, int64_t late, RdKafka::Message * message, DetectorCallbacksType & callbacks);

/// \brief Main processing function for AR51 data
/// \return number of processed events _in_ the message
  uint32_t process_AR51_data(RdKafka::Message * Msg, DetectorCallbacksType & callbacks);

}

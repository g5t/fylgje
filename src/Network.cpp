// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief Network data structures for the ESS readout system and interface details for Kafka service
//===----------------------------------------------------------------------===//
#include "Network.h"
#include <algorithm>
#include <fmt/format.h>
#include <iostream>
#include <unistd.h>
#include <vector>
#include <tuple>

using namespace ess::network;

// Copied from daqlite - modified to not reinstantiate charset 'length' times
static std::string randomGroupString(size_t length) {
  srand(getpid());
  const char charset[] = "0123456789"
                         "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                         "abcdefghijklmnopqrstuvwxyz";
  const size_t max_index = (sizeof(charset) - 1);
  auto randchar = [&charset]() -> char {
    return charset[rand() % max_index];
  };
  std::string str(length, 0);
  std::generate_n(str.begin(), length, randchar);
  return str;
}


RdKafka::KafkaConsumer * ess::network::subscribe_topic(
    Configuration & Config,
    const std::vector<std::pair<std::string, std::string>> & kafkaConfig
    ){
  auto mConf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);
  if (!mConf) {
    fmt::print("Unable to create global Conf object\n");
    return nullptr;
  }
  std::string ErrStr;
  /// \todo figure out good values for these
  /// \todo some may be obsolete
  mConf->set("metadata.broker.list", Config.Kafka.Broker, ErrStr);
  mConf->set("message.max.bytes", Config.Kafka.MessageMaxBytes, ErrStr);
  mConf->set("fetch.message.max.bytes", Config.Kafka.FetchMessagMaxBytes, ErrStr);
  mConf->set("replica.fetch.max.bytes", Config.Kafka.ReplicaFetchMaxBytes, ErrStr);
  mConf->set("group.id", randomGroupString(16u), ErrStr);
  mConf->set("enable.auto.commit", Config.Kafka.EnableAutoCommit, ErrStr);
  mConf->set("enable.auto.offset.store", Config.Kafka.EnableAutoOffsetStore, ErrStr);

  for (auto &Config : kafkaConfig) {
    mConf->set(Config.first, Config.second, ErrStr);
  }
  auto ret = RdKafka::KafkaConsumer::create(mConf, ErrStr);
  if (!ret) {
    fmt::print("Failed to create consumer: {}\n", ErrStr);
    return nullptr;
  }
  //
//  // // Start consumer for topic+partition at start offset
//  std::cout << "Subscribe to topic " << configuration.Kafka.Topic << "\n";
//  RdKafka::ErrorCode resp = ret->subscribe({configuration.Kafka.Topic});
//  if (resp != RdKafka::ERR_NO_ERROR) {
//    fmt::print("Failed to subscribe consumer to '{}': {}\n", configuration.Kafka.Topic, err2str(resp));
//  }
  return ret;
}



static void set_topic_partition_offset(
    Configuration & configuration,
    int32_t partition,
    RdKafka::KafkaConsumer * consumer,
    std::vector<RdKafka::TopicPartition*>& tps,
    Start start,
    int64_t ms_since_utc_epoch
){
  int64_t low{0}, high{0};
  auto resp = consumer->get_watermark_offsets(configuration.Kafka.Topic, partition, &low, &high);
  if (resp != RdKafka::ERR_NO_ERROR) {
    fmt::print("Failed remembering watermark offsets for {} (partition {}): {}\n", configuration.Kafka.Topic, partition, err2str(resp));
  }
  if (low == high) {
    resp = consumer->query_watermark_offsets(configuration.Kafka.Topic, partition, &low, &high, 1000);
    if (resp != RdKafka::ERR_NO_ERROR) {
      fmt::print("Failed retrieving watermark offsets for {} (partition {}): {}\n", configuration.Kafka.Topic, partition, err2str(resp));
    }
  }
  fmt::print("Valid offsets for {} (partition: {}) are in range ({}, {})\n", configuration.Kafka.Topic, partition, low, high);

  tps.front()->set_offset(start == Start::Beginning ? low : start == Start::End ? high : ms_since_utc_epoch);
  if (start == Start::Time){
    // now handle converting a time to an offset
    resp = consumer->offsetsForTimes(tps, 1000);
    if (resp != RdKafka::ERR_NO_ERROR){
      fmt::print("Failed retrieving soonest offset after {} for {} (partition {}):  {}\n",
                 ms_since_utc_epoch, configuration.Kafka.Topic, partition, err2str(resp));
    }
  }
  fmt::print("Offset set to {}\n", tps.front()->offset());
}



int32_t ess::network::set_consumer_offset(
    Configuration & configuration,
    RdKafka::KafkaConsumer * consumer,
    Start start,
    int64_t ms_since_utc_epoch
){
  // set the consumer starting point, using the partition's known offsets ...
  RdKafka::Topic *only_rkt{nullptr};
  RdKafka::Metadata *metadataptr;
  int32_t partition{0};

  auto resp = consumer->metadata(true, only_rkt, &metadataptr, 1000);
  if (resp != RdKafka::ERR_NO_ERROR) {
    fmt::print("Failed retrieving metadata: {}\n", err2str(resp));
  }
  if (metadataptr == nullptr) {
    fmt::print("metadataptr still NULL\n");
  } else {
    auto topic_metadata = metadataptr->topics();
    fmt::print("Got metadata about on {} topics\n", topic_metadata->size());
    for (const auto &topic_meta: *topic_metadata) {
      if (topic_meta->topic() == configuration.Kafka.Topic) {
        fmt::print(" {} has {} partitions [", topic_meta->topic(), topic_meta->partitions()->size());
        const auto &partitions = topic_meta->partitions();
        for (const auto &partition: *partitions) {
          fmt::print(" {},", partition->id());
        }
        fmt::print("]\n");
        // pick one at random? or the first one?
        partition = topic_meta->partitions()->front()->id();
      }
    }
  }
  std::vector<std::string> subscriptions;
  resp = consumer->subscription(subscriptions);
  if (resp != RdKafka::ERR_NO_ERROR){
    fmt::print("Failed to retrieve subscribed topics: {}\n", err2str(resp));
  } else {
    fmt::print("Subscribed to [");
    for (const auto & sub: subscriptions) fmt::print("{}, ", sub);
    fmt::print("]\n");
  }

  std::vector<RdKafka::TopicPartition*> tps;
  tps.push_back(RdKafka::TopicPartition::create(configuration.Kafka.Topic, partition));
  set_topic_partition_offset(configuration, partition, consumer, tps, start, ms_since_utc_epoch);
  consumer->assign(tps); // since consumption hasn't started, we seek by assigning the (topic, partition, offset)

  return partition;
}


int64_t ess::network::consume_all(
    Configuration & configuration,
    int32_t partition,
    RdKafka::KafkaConsumer * consumer
){
  std::vector<RdKafka::TopicPartition*> tps;
  consumer->assignment(tps);
  set_topic_partition_offset(configuration, partition, consumer, tps, Beginning, 0);
  consumer->seek(*tps.front(), 1);
  return 0;
}

int64_t ess::network::consume_from(
    Configuration & configuration,
    int32_t partition,
    RdKafka::KafkaConsumer * consumer,
    std::optional<kafka::time::milliseconds> since_epoch
){
  auto earliest_timestamp = since_epoch.has_value() ? since_epoch.value().count() : 0;
  std::vector<RdKafka::TopicPartition*> tps;
  consumer->assignment(tps);
  set_topic_partition_offset(configuration, partition, consumer, tps, Time, earliest_timestamp);
  consumer->seek(*tps.front(), 1);
  return earliest_timestamp;
}

int64_t ess::network::consume_until(
    Configuration & configuration,
    int32_t partition,
    RdKafka::KafkaConsumer * consumer,
    int64_t early,
    std::optional<kafka::time::milliseconds> since_epoch
){
  auto duration = std::chrono::system_clock::now().time_since_epoch();
  auto ms_now= std::chrono::duration_cast<std::chrono::milliseconds>(duration);
  auto latest_timestamp = since_epoch.has_value() ? since_epoch.value().count() : -1;
  if (since_epoch < ms_now){
    // consume only in the past; do we _need_ to seek backwards?
    std::vector<RdKafka::TopicPartition*> tps;
    consumer->assignment(tps);
    set_topic_partition_offset(configuration, partition, consumer, tps, early < 0 ? Beginning : Time, early);
    consumer->seek(*tps.front(), 1);
  }
  return latest_timestamp;
}

std::tuple<Status, uint32_t> ess::network::handle_message(int64_t early, int64_t late, RdKafka::Message *message, DetectorCallbacksType & callbacks) {
  switch (message->err()) {
    case RdKafka::ERR__TIMED_OUT:
      return {Continue, 0};

    case RdKafka::ERR_NO_ERROR: {
      uint32_t count{0};
      auto message_timestamp = message->timestamp().timestamp;
      if (late < 0 || message_timestamp < late) {
        if (RawReadoutMessageBufferHasIdentifier(message->payload())) {
          count = process_AR51_data(message, callbacks);
        } else {
          fmt::print("Not a ar51 Kafka message!\n");
        }
      } else if (message_timestamp >= late) {
        return {Halt, RawReadoutMessageBufferHasIdentifier(message->payload()) ? 1 : 0};
      } else {
        fmt::print("Message timestamp {} is not within range {} to {}?\n", message_timestamp, early, late);
      }
      return {count ? Update : Continue, 1}; // whether or not it had events, this is an AR51 message
    }
    case RdKafka::ERR__PARTITION_EOF: {
      fmt::print("Reached end of partition\n");
      return {Halt, 0};
    }
    default:
      fmt::print("Consume failed: {}", message->errstr());
      return {Halt, 0};
  }
}


/// Main processing function for AR51 data
uint32_t ess::network::process_AR51_data(RdKafka::Message *Msg, DetectorCallbacksType & callbacks) {
  // First check header
  const auto & RawReadoutMsg = GetRawReadoutMessage(Msg->payload());
  auto MsgSize = static_cast<int>(RawReadoutMsg->raw_data()->size());
  auto * Header = (struct PacketHeaderV0 *)RawReadoutMsg->raw_data()->Data();
  if ((Header->CookieAndType & 0xffffff) != 0x535345) {
    printf("Non-ESS readout (cookie 0x%08x)\n", Header->CookieAndType);
    return 0;
  }
  if (Header->TotalLength !=  MsgSize) {
    printf("Readout size mismatch\n");
    return 0;
  }
  if (MsgSize == sizeof(struct PacketHeaderV0)) {
    return 0;
  }
  auto Type = Header->CookieAndType >> 28;
  auto pulse_hi = Header->PulseHigh;
  auto pulse_lo = Header->PulseLow;
  auto prev_hi = Header->PrevPulseHigh;
  auto prev_lo = Header->PrevPulseLow;

  uint8_t * DataPtr = (uint8_t * )Header + 30;
  if (Header->Version == 1) {
    DataPtr += 2;
  }
  //TODO Is this correct for Version 1 headers too?
  auto DataLength = Header->TotalLength - sizeof(struct PacketHeaderV0);

  // Dispatch technology specific
  if (callbacks.find(Type) != callbacks.end()){
    return callbacks[Type](DataPtr, static_cast<int>(DataLength), pulse_hi, pulse_lo, prev_hi, prev_lo);
  } else {
    fmt::print("Unregistered readout Type {}\n", Type);
  };
  return 0;
}


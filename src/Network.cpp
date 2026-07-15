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
//
// // Copied from daqlite - modified to not reinstantiate charset 'length' times
// static std::string randomGroupString(const size_t length) {
//   srand(getpid());
//   constexpr char charset[] = "0123456789"
//                          "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
//                          "abcdefghijklmnopqrstuvwxyz";
//   constexpr size_t max_index = (sizeof(charset) - 1);
//   auto a_random_character = []() -> char {
//     return charset[rand() % max_index];
//   };
//   std::string str(length, 0);
//   std::generate_n(str.begin(), length, a_random_character);
//   return str;
// }


RdKafka::KafkaConsumer * ess::network::subscribe_topic(
    const Configuration & Config,
    const std::vector<std::pair<std::string, std::string>> & kafkaConfig
    ){
  const auto mConf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);
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
  const auto group_id = fmt::format("Groupid (pid) {}", getpid());
  mConf->set("group.id", group_id, ErrStr);
  mConf->set("enable.auto.commit", Config.Kafka.EnableAutoCommit, ErrStr);
  mConf->set("enable.auto.offset.store", Config.Kafka.EnableAutoOffsetStore, ErrStr);
  // without this, ERR__PARTITION_EOF is never delivered and a finite-window
  // consumer has no way to learn that a partition is exhausted
  mConf->set("enable.partition.eof", "true", ErrStr);

  for (const auto &[name, value] : kafkaConfig) {
    mConf->set(name, value, ErrStr);
  }
  const auto ret = RdKafka::KafkaConsumer::create(mConf, ErrStr);
  delete mConf;
  if (!ret) {
    fmt::print("Failed to create consumer: {}\n", ErrStr);
    return nullptr;
  }
  // **DO NOT** subscribe to the topic -- we need to set the offset _before_ *ASSIGNING* the topic.
  return ret;
}

void ess::network::PartitionWindow::reset(RdKafka::KafkaConsumer * consumer) {
  states_.clear();
  std::vector<RdKafka::TopicPartition*> tps;
  consumer->assignment(tps);
  for (const auto tp: tps) {
    states_[tp->partition()] = {};
  }
  RdKafka::TopicPartition::destroy(tps);
}



static void set_topic_partition_offset(
    Configuration & configuration,
    RdKafka::KafkaConsumer * consumer,
    std::vector<RdKafka::TopicPartition *>& tps,
    const Start start,
    const int64_t ms_since_utc_epoch
){
  auto set_offset = [&](RdKafka::TopicPartition* tp) {
    int64_t low{0}, high{0};
    const auto partition = tp->partition();
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
    tp->set_offset(start == Start::Beginning ? low : start == Start::End ? high : ms_since_utc_epoch);
    return std::make_pair(low, high);
  };

  // purely ascetic sorting
  std::sort(tps.begin(), tps.end(), [](const RdKafka::TopicPartition* a, const RdKafka::TopicPartition* b) {
    return a->partition() < b->partition();
  });

  std::map<int, std::pair<int64_t, int64_t>> low_high;
  for (const auto tp: tps){
    low_high.insert({tp->partition(), set_offset(tp)});
  }

  if (start == Start::Time){
    // now handle converting a time to an offset
    if (const auto resp = consumer->offsetsForTimes(tps, 1000); resp != RdKafka::ERR_NO_ERROR){
      fmt::print("Failed retrieving soonest offset after {} for {}:  {}\n",
                 ms_since_utc_epoch, configuration.Kafka.Topic, err2str(resp));
    }
    // A negative offset means either no message at-or-after the requested
    // time, or a broker without timestamp lookup (librdkafka's mock cluster).
    // Fall back to the low watermark: handle_message drops out-of-window
    // messages by timestamp, so this is correct either way.
    for (const auto tp: tps){
      if (tp->offset() < 0) {
        tp->set_offset(low_high.at(tp->partition()).first);
      }
    }
  }

  std::stringstream ss;
  ss << "Offsets for " << configuration.Kafka.Topic << " (partition: [min, offset, max]) are [\n";
  for (const auto tp: tps){
    const auto &[low, high] = low_high.at(tp->partition());
    ss << "  " << tp->partition() << ": [" << low << ", " << tp->offset() << ", " << high << "],\n";
  }
  if (!tps.empty()) ss.seekp(-2, ss.cur);
  ss << "\n]\n";
  fmt::print(ss.str());
}


void ess::network::set_consumer_offset(
    Configuration & configuration,
    RdKafka::KafkaConsumer * consumer,
    const Start start,
    const int64_t ms_since_utc_epoch
){
  // set the consumer starting point, using the partition's known offsets ...
  const RdKafka::Topic *only_rkt{nullptr};
  RdKafka::Metadata *metadata_ptr{nullptr};
  std::vector<int32_t> partitions;

  auto resp = consumer->metadata(true, only_rkt, &metadata_ptr, 1000);
  if (resp != RdKafka::ERR_NO_ERROR) {
    fmt::print("Failed retrieving metadata: {}\n", err2str(resp));
  }
  if (metadata_ptr == nullptr) {
    fmt::print("metadata_ptr still NULL\n");
  } else {
    const auto topic_metadata = metadata_ptr->topics();
    fmt::print("Got metadata about on {} topics\n", topic_metadata->size());
    for (const auto &topic_meta: *topic_metadata) {
      if (topic_meta->topic() == configuration.Kafka.Topic) {
        fmt::print(" {} has {} partitions [", topic_meta->topic(), topic_meta->partitions()->size());
        const auto &partitions_metadata = topic_meta->partitions();
        for (const auto &partition_metadata: *partitions_metadata) {
          fmt::print(" {},", partition_metadata->id());
          // keep track of the partition numbers (in case they're not contiguous)
          partitions.push_back(partition_metadata->id());
        }
        fmt::print("]\n");
      }
    }
    delete metadata_ptr;
  }

  std::vector<RdKafka::TopicPartition*> tps;
  for (const auto & partition: partitions) {
    tps.push_back(RdKafka::TopicPartition::create(configuration.Kafka.Topic, partition));
  }
  set_topic_partition_offset(configuration, consumer, tps, start, ms_since_utc_epoch);
  consumer->assign(tps); // since consumption hasn't started, we seek by assigning the (topic, partition, offset)
  RdKafka::TopicPartition::destroy(tps); // assign() copies the list
}


int64_t ess::network::consume_all(
    Configuration & configuration,
    RdKafka::KafkaConsumer * consumer
){
  std::vector<RdKafka::TopicPartition*> tps;
  consumer->assignment(tps);
  if (tps.empty()) {
    set_consumer_offset(configuration, consumer, Beginning, 0);
  } else {
    consumer->resume(tps);
    set_topic_partition_offset(configuration, consumer, tps, Beginning, 0);
    for (const auto & tp: tps){
      if (const auto resp = consumer->seek(*tp, 5000); resp != RdKafka::ERR_NO_ERROR) {
        fmt::print("Failed seeking {} partition {} to offset {}: {}\n",
                   configuration.Kafka.Topic, tp->partition(), tp->offset(), err2str(resp));
      }
    }
  }
  RdKafka::TopicPartition::destroy(tps);
  return 0;
}

int64_t ess::network::consume_from(
    Configuration & configuration,
    RdKafka::KafkaConsumer * consumer,
    const std::optional<kafka::time::milliseconds> since_epoch
){
  const auto earliest_timestamp = since_epoch.has_value() ? since_epoch.value().count() : 0;
  std::vector<RdKafka::TopicPartition*> tps;
  consumer->assignment(tps);
  if (tps.empty()) {
    // first positioning: discover partitions, set offsets, and assign in one go
    set_consumer_offset(configuration, consumer, Time, earliest_timestamp);
  } else {
    // re-windowing an active consumer (GUI): resume anything paused and seek
    consumer->resume(tps);
    set_topic_partition_offset(configuration, consumer, tps, Time, earliest_timestamp);
    for (const auto & tp: tps){
      if (const auto resp = consumer->seek(*tp, 5000); resp != RdKafka::ERR_NO_ERROR) {
        fmt::print("Failed seeking {} partition {} to offset {}: {}\n",
                   configuration.Kafka.Topic, tp->partition(), tp->offset(), err2str(resp));
      }
    }
  }
  RdKafka::TopicPartition::destroy(tps);
  return earliest_timestamp;
}

int64_t ess::network::consume_until(
    Configuration & /*configuration*/,
    RdKafka::KafkaConsumer * /*consumer*/,
    int64_t /*early*/,
    std::optional<kafka::time::milliseconds> since_epoch
){
  // positioning is consume_from's job; this only records the window end
  return since_epoch.has_value() ? since_epoch.value().count() : -1;
}

std::tuple<Status, uint32_t> ess::network::handle_message(
    RdKafka::KafkaConsumer * consumer,
    const int64_t early, const int64_t late,
    RdKafka::Message * message,
    CallbacksType & callbacks,
    PartitionWindow & window
) {
  switch (message->err()) {
    case RdKafka::ERR__TIMED_OUT:
      return {Continue, 0};

    case RdKafka::ERR_NO_ERROR: {
      const auto partition = message->partition();
      if (window.is_done(partition)) {
        // straggler from a partition already past the window end
        return {Continue, 0};
      }
      window.clear_eof(partition); // new data: no longer caught up
      if (const auto message_timestamp = message->timestamp().timestamp; late >= 0 && message_timestamp >= late) {
        // this partition has passed the window end; others may not have.
        // pause it so we stop fetching its (out-of-window) tail
        window.mark_done(partition);
        std::vector<RdKafka::TopicPartition*> tp{
            RdKafka::TopicPartition::create(message->topic_name(), partition)};
        consumer->pause(tp);
        RdKafka::TopicPartition::destroy(tp);
        return {window.all_done() ? Halt : Continue, 0};
      } else if (message_timestamp < early && early >= 0 && message_timestamp >= 0) {
        // seen when the seek fell back to the low watermark; drop silently
        return {Continue, 0};
      }
      if (!RawReadoutMessageBufferHasIdentifier(message->payload())) {
        fmt::print("Not a ar51 Kafka message!\n");
        return {Continue, 0};
      }
      auto [count, bad] = process_AR51_data(message, callbacks);
      return {count ? Update : Continue, 1}; // whether or not it had events, this is an AR51 message
    }
    case RdKafka::ERR__PARTITION_EOF: {
      if (late < 0) {
        // live mode: the partition may receive more data, keep polling
        return {Continue, 0};
      }
      if (kafka::time::now_milliseconds().count() < late) {
        // the window is still open: this partition is caught up for now, but
        // in-window messages may yet arrive -- keep polling
        window.mark_eof(message->partition());
        return {Continue, 0};
      }
      window.mark_done(message->partition());
      fmt::print("Reached end of partition {}\n", message->partition());
      return {window.all_done() ? Halt : Continue, 0};
    }
    default:
      fmt::print("Consume failed: {}\n", message->errstr());
      return {Halt, 0};
  }
}


/// Main processing function for AR51 data
std::tuple<uint32_t, uint32_t> ess::network::process_AR51_data(const RdKafka::Message *Msg, CallbacksType & callbacks) {
  // First check header
  const auto & RawReadoutMsg = GetRawReadoutMessage(Msg->payload());
  const auto MsgSize = static_cast<int>(RawReadoutMsg->raw_data()->size());
  auto * Header = reinterpret_cast<const struct PacketHeaderV0 *>(RawReadoutMsg->raw_data()->Data());
  if ((Header->CookieAndType & 0xffffff) != 0x535345) {
    printf("Non-ESS readout (cookie 0x%08x)\n", Header->CookieAndType);
    return {0, 1};
  }
  if (Header->TotalLength !=  MsgSize) {
    printf("Readout size mismatch\n");
    return {0, 1};
  }
  size_t common_header_length{0};
  if (Header->Version == 1) {
    common_header_length = sizeof(struct PacketHeaderV1);
  } else {
    common_header_length = sizeof(struct PacketHeaderV0);
  }
  if (MsgSize == static_cast<int>(common_header_length)) {
    return {0, 1};
  }

  auto * DataPtr = reinterpret_cast<const uint8_t *>(Header) + common_header_length;
  const auto DataLength = Header->TotalLength - common_header_length;
  auto Type = Header->CookieAndType >> 28;

  // extract the pulse time information from the header to pass to the technology specific callback
  const PulseTimes times{Header->PulseHigh, Header->PulseLow, Header->PrevPulseHigh, Header->PrevPulseLow};
  // Dispatch technology specific
  if (callbacks.find(Type) != callbacks.end()){
    return {callbacks[Type](DataPtr, static_cast<int>(DataLength), &times), 0};
  }
  fmt::print("Unregistered readout Type {}\n", Type);;
  return {0, 1};
}


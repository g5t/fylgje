// Copyright (C) 2022-2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief using nlohmann json parser to read configurations from file
//===----------------------------------------------------------------------===//

#include "KafkaConfig.h"
#include "JsonFile.h"
#include "Verbosity.h"
#include <algorithm>
#include <array>
#include <condition_variable>
#include <iostream>
#include <cstdio>
#include <fmt/format.h>

namespace {
// fylgje only ever consumes; librdkafka warns about (and ignores) any
// producer-only property, so drop them here instead. There is no librdkafka
// API to query a property's scope, hence an explicit list.
constexpr std::array producer_only_properties{
    "queue.buffering.max.ms",
    "queue.buffering.max.messages",
    "queue.buffering.max.kbytes",
    "queue.buffering.backpressure.threshold",
    "linger.ms",
    "batch.num.messages",
    "batch.size",
    "message.timeout.ms",
    "delivery.timeout.ms",
    "request.required.acks",
    "acks",
    "retries",
    "message.send.max.retries",
    "enable.idempotence",
    "compression.codec",
    "compression.type",
    "compression.level",
    "partitioner",
    "sticky.partitioning.linger.ms",
};

bool is_producer_only(const std::string & name) {
  return std::find(producer_only_properties.begin(), producer_only_properties.end(), name)
         != producer_only_properties.end();
}
}

KafkaConfig::KafkaConfig(const std::string & KafkaConfigFile) {
  if (KafkaConfigFile.empty()) {
    return;
  }
  if (fylgje::log::debug()) {
    fmt::print("KAFKA CONFIG FROM FILE\n");
  }
  nlohmann::json root = from_json_file(KafkaConfigFile);

  try {
    nlohmann::json KafkaParms = root["KafkaParms"];

    for (const auto &Parm : KafkaParms) {
      std::map<std::string, std::string> MyMap = Parm;
      for (auto it = MyMap.begin(); it != MyMap.end(); ++it) {
        if (is_producer_only(it->first)) {
          if (fylgje::log::debug()) {
            fmt::print("Ignoring producer-only Kafka property '{}' for this consumer\n", it->first);
          }
          continue;
        }
        std::pair<std::string, std::string> CfgPair{it->first, it->second};
        CfgParms.push_back(CfgPair);
      }
    }

  } catch (...) {
    fmt::print("Kafka JSON config - error: Invalid Json file: {}\n", KafkaConfigFile);
    throw std::runtime_error("Invalid Json file for Kafka config");
  }
}

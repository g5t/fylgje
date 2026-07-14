// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief RAII wrapper around librdkafka's in-process mock Kafka cluster
//===----------------------------------------------------------------------===//
#pragma once
#include <stdexcept>
#include <string>
#include <librdkafka/rdkafka.h>
#include <librdkafka/rdkafka_mock.h>

namespace fylgje::test {

/// \brief An in-process Kafka cluster: real protocol over real sockets, no
///        external broker needed. Anything that can reach the bootstrap
///        address (including other processes) can produce/consume against it.
class MockKafkaCluster {
public:
  explicit MockKafkaCluster(const int broker_count = 1) {
    char errstr[512];
    auto * conf = rd_kafka_conf_new();
    rd_kafka_conf_set(conf, "client.id", "fylgje-mock-cluster", errstr, sizeof errstr);
    rk_ = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, sizeof errstr); // owns conf
    if (rk_ == nullptr) {
      throw std::runtime_error(std::string{"rd_kafka_new failed: "} + errstr);
    }
    cluster_ = rd_kafka_mock_cluster_new(rk_, broker_count);
    if (cluster_ == nullptr) {
      rd_kafka_destroy(rk_);
      throw std::runtime_error("rd_kafka_mock_cluster_new failed");
    }
    bootstraps_ = rd_kafka_mock_cluster_bootstraps(cluster_);
  }

  MockKafkaCluster(const MockKafkaCluster &) = delete;
  MockKafkaCluster & operator=(const MockKafkaCluster &) = delete;

  ~MockKafkaCluster() {
    if (cluster_ != nullptr) rd_kafka_mock_cluster_destroy(cluster_);
    if (rk_ != nullptr) rd_kafka_destroy(rk_);
  }

  void create_topic(const std::string & topic, const int partitions, const int replication = 1) const {
    if (const auto err = rd_kafka_mock_topic_create(cluster_, topic.c_str(), partitions, replication);
        err != RD_KAFKA_RESP_ERR_NO_ERROR) {
      throw std::runtime_error(std::string{"rd_kafka_mock_topic_create failed: "} + rd_kafka_err2str(err));
    }
  }

  [[nodiscard]] const std::string & bootstraps() const { return bootstraps_; }

private:
  rd_kafka_t * rk_{nullptr};
  rd_kafka_mock_cluster_t * cluster_{nullptr};
  std::string bootstraps_;
};

}

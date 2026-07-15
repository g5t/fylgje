// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief Integration tests for ESSConsumer against an in-process mock Kafka
///        cluster: no external broker required
//===----------------------------------------------------------------------===//
#include <chrono>
#include <memory>
#include <thread>
#include <gtest/gtest.h>
#include <librdkafka/rdkafkacpp.h>

#include "ESSConsumer.h"
#include "MockKafkaCluster.h"
#include "SyntheticData.h"

namespace {

using fylgje::test::MockKafkaCluster;
using fylgje::test::SyntheticMessage;
using fylgje::test::SyntheticStream;

/// \brief Minimal EventManager stand-in: counts what parseCAENData accepts
struct CountingSink {
  int64_t readouts{0};
  void add(int /*fiber*/, int /*group*/, int /*a*/, int /*b*/,
           double /*time*/, uint32_t /*high*/, uint32_t /*low*/) {
    ++readouts;
  }
};

void produce_stream(const std::string & brokers, const std::string & topic,
                    const std::vector<SyntheticMessage> & messages) {
  std::string errstr;
  const std::unique_ptr<RdKafka::Conf> conf{RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL)};
  conf->set("bootstrap.servers", brokers, errstr);
  const std::unique_ptr<RdKafka::Producer> producer{RdKafka::Producer::create(conf.get(), errstr)};
  ASSERT_NE(producer, nullptr) << errstr;
  for (const auto & message: messages) {
    const auto resp = producer->produce(
        topic, message.partition, RdKafka::Producer::RK_MSG_COPY,
        const_cast<uint8_t *>(message.payload.data()), message.payload.size(),
        nullptr, 0, message.timestamp_ms, nullptr);
    ASSERT_EQ(resp, RdKafka::ERR_NO_ERROR) << RdKafka::err2str(resp);
    producer->poll(0);
  }
  ASSERT_EQ(producer->flush(10'000), RdKafka::ERR_NO_ERROR);
}

class ConsumerMockClusterTest : public ::testing::Test {
protected:
  static constexpr auto topic = "synthetic-ar51";
  MockKafkaCluster cluster{};
  SyntheticStream stream{};
  Configuration config;

  void SetUp() override {
    cluster.create_topic(topic, stream.n_partitions);
    produce_stream(cluster.bootstraps(), topic, stream.messages());
    if (::testing::Test::HasFatalFailure()) return;
    config.Kafka.Broker = cluster.bootstraps();
    config.Kafka.Topic = topic;
    config.KafkaConfigFile = "";
  }

  /// \brief run a finite-window consumer to completion
  /// \return (ar51 message count, accepted readout count, wall-clock seconds)
  std::tuple<int64_t, int64_t, double> run_window(const int64_t from_ms, const int64_t to_ms) {
    const auto sink = std::make_shared<CountingSink>();
    const auto start = std::chrono::steady_clock::now();
    ESSConsumer<CountingSink> consumer{sink, config,
                                       kafka::time::milliseconds{from_ms},
                                       kafka::time::milliseconds{to_ms}};
    consumer.setMaximumIdle(std::chrono::milliseconds{5'000});
    consumer.run();
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_EQ(consumer.event_count(), sink->readouts);
    return {consumer.message_count(), consumer.event_count(), elapsed.count()};
  }
};

/// The mock cluster supports every Kafka API the consumer relies on
TEST_F(ConsumerMockClusterTest, MockClusterSmoke) {
  std::string errstr;
  const std::unique_ptr<RdKafka::Conf> conf{RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL)};
  conf->set("bootstrap.servers", cluster.bootstraps(), errstr);
  conf->set("group.id", "smoke-test", errstr);
  const std::unique_ptr<RdKafka::KafkaConsumer> consumer{RdKafka::KafkaConsumer::create(conf.get(), errstr)};
  ASSERT_NE(consumer, nullptr) << errstr;

  // metadata reports the topic with the expected partition count
  RdKafka::Metadata * metadata{nullptr};
  ASSERT_EQ(consumer->metadata(true, nullptr, &metadata, 5'000), RdKafka::ERR_NO_ERROR);
  ASSERT_NE(metadata, nullptr);
  bool found{false};
  for (const auto * topic_meta: *metadata->topics()) {
    if (topic_meta->topic() == topic) {
      found = true;
      EXPECT_EQ(static_cast<int>(topic_meta->partitions()->size()), stream.n_partitions);
    }
  }
  EXPECT_TRUE(found);
  delete metadata;

  // watermarks: messages are distributed round-robin, so evenly
  int64_t low{-1}, high{-1};
  ASSERT_EQ(consumer->query_watermark_offsets(topic, 0, &low, &high, 5'000), RdKafka::ERR_NO_ERROR);
  EXPECT_EQ(low, 0);
  EXPECT_EQ(high, stream.n_messages / stream.n_partitions);

  // messages retain the timestamps they were produced with
  std::vector<RdKafka::TopicPartition *> assign_tps{RdKafka::TopicPartition::create(topic, 0, 0)};
  consumer->assign(assign_tps);
  RdKafka::TopicPartition::destroy(assign_tps);
  const std::unique_ptr<RdKafka::Message> first{consumer->consume(5'000)};
  ASSERT_EQ(first->err(), RdKafka::ERR_NO_ERROR) << first->errstr();
  EXPECT_EQ(first->timestamp().timestamp, stream.t0_ms);
  consumer->unassign();

  // The mock broker does NOT implement timestamp-based offset lookup: it
  // answers offsetsForTimes with offset -1 and no error, even though messages
  // at/after the requested time exist. The consumer therefore falls back to
  // the low watermark and filters per-message by timestamp; if this assertion
  // ever starts failing with a real offset (here it would be 4), the mock has
  // gained timestamp lookup and the fallback is no longer exercised by these tests.
  std::vector<RdKafka::TopicPartition *> tps{
      RdKafka::TopicPartition::create(topic, 0, stream.t0_ms + 10 * stream.dt_ms)};
  ASSERT_EQ(consumer->offsetsForTimes(tps, 5'000), RdKafka::ERR_NO_ERROR);
  EXPECT_EQ(tps[0]->err(), RdKafka::ERR_NO_ERROR) << RdKafka::err2str(tps[0]->err());
  EXPECT_EQ(tps[0]->offset(), -1);
  RdKafka::TopicPartition::destroy(tps);

  consumer->close();
}

TEST_F(ConsumerMockClusterTest, FullWindow) {
  const auto [messages, readouts, seconds] = run_window(stream.t0_ms - 1'000, stream.end_ms() + 1'000);
  EXPECT_EQ(messages, stream.n_messages);
  EXPECT_EQ(readouts, static_cast<int64_t>(stream.n_messages) * stream.readouts_per_message);
  EXPECT_LT(seconds, 30.0);
}

TEST_F(ConsumerMockClusterTest, SubWindow) {
  const auto from = stream.t0_ms + 10 * stream.dt_ms;
  const auto to = stream.t0_ms + 20 * stream.dt_ms;
  const auto expected = stream.count_in_window(from, to);
  ASSERT_GT(expected, 0);
  ASSERT_LT(expected, stream.n_messages);
  const auto [messages, readouts, seconds] = run_window(from, to);
  EXPECT_EQ(messages, expected);
  EXPECT_EQ(readouts, static_cast<int64_t>(expected) * stream.readouts_per_message);
  EXPECT_LT(seconds, 30.0);
}

TEST_F(ConsumerMockClusterTest, EmptyWindowBeforeData) {
  const auto [messages, readouts, seconds] = run_window(stream.t0_ms - 10'000, stream.t0_ms - 5'000);
  EXPECT_EQ(messages, 0);
  EXPECT_EQ(readouts, 0);
  EXPECT_LT(seconds, 30.0);
}

TEST_F(ConsumerMockClusterTest, EmptyWindowAfterData) {
  const auto [messages, readouts, seconds] = run_window(stream.end_ms() + 10'000, stream.end_ms() + 20'000);
  EXPECT_EQ(messages, 0);
  EXPECT_EQ(readouts, 0);
  EXPECT_LT(seconds, 30.0);
}

/// A window end in the future keeps the consumer alive until wall-clock
/// reaches it, even though every partition hits EOF within the first second
TEST_F(ConsumerMockClusterTest, FutureWindowWaitsForClose) {
  SyntheticStream live = stream;
  live.t0_ms = kafka::time::now_milliseconds().count();
  live.dt_ms = 100; // all messages well inside the window
  const auto live_topic = "synthetic-ar51-future";
  cluster.create_topic(live_topic, live.n_partitions);
  produce_stream(cluster.bootstraps(), live_topic, live.messages());
  config.Kafka.Topic = live_topic;

  const auto [messages, readouts, seconds] = run_window(live.t0_ms - 1'000, live.t0_ms + 6'000);
  EXPECT_EQ(messages, live.n_messages);
  EXPECT_EQ(readouts, static_cast<int64_t>(live.n_messages) * live.readouts_per_message);
  // caught up almost immediately, but must have waited for the window to close
  EXPECT_GT(seconds, 4.5);
  EXPECT_LT(seconds, 20.0);
}

/// Messages produced while a future-window consumer is already caught up
/// (post-EOF) are still collected
TEST_F(ConsumerMockClusterTest, FutureWindowPicksUpLiveMessages) {
  SyntheticStream batch_a = stream;
  batch_a.t0_ms = kafka::time::now_milliseconds().count();
  batch_a.dt_ms = 100;
  const auto live_topic = "synthetic-ar51-live";
  cluster.create_topic(live_topic, batch_a.n_partitions);
  produce_stream(cluster.bootstraps(), live_topic, batch_a.messages());
  config.Kafka.Topic = live_topic;

  const auto from = batch_a.t0_ms - 1'000;
  const auto to = batch_a.t0_ms + 8'000;
  const auto sink = std::make_shared<CountingSink>();
  int64_t messages{-1}, readouts{-1};
  const auto start = std::chrono::steady_clock::now();
  std::thread consumer_thread([&] {
    ESSConsumer<CountingSink> consumer{sink, config,
                                       kafka::time::milliseconds{from},
                                       kafka::time::milliseconds{to}};
    consumer.setMaximumIdle(std::chrono::milliseconds{5'000});
    consumer.run();
    messages = consumer.message_count();
    readouts = consumer.event_count();
  });

  // let the consumer catch up (EOF) before producing the second batch
  std::this_thread::sleep_for(std::chrono::seconds(2));
  SyntheticStream batch_b = batch_a;
  batch_b.t0_ms = kafka::time::now_milliseconds().count();
  batch_b.n_messages = 15;
  produce_stream(cluster.bootstraps(), live_topic, batch_b.messages());

  consumer_thread.join();
  const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
  EXPECT_EQ(messages, batch_a.n_messages + batch_b.n_messages);
  EXPECT_EQ(readouts, static_cast<int64_t>(batch_a.n_messages + batch_b.n_messages) * batch_a.readouts_per_message);
  EXPECT_GT(elapsed.count(), 6.5);
  EXPECT_LT(elapsed.count(), 25.0);
}

/// A live (no end time) consumer halts promptly when its stop flag is raised,
/// as the CLI's SIGINT handler does, with everything consumed so far intact
TEST_F(ConsumerMockClusterTest, StopFlagHaltsLiveConsumer) {
  const auto sink = std::make_shared<CountingSink>();
  std::atomic<bool> stop{false};
  int64_t messages{-1};
  const auto start = std::chrono::steady_clock::now();
  std::thread consumer_thread([&] {
    // live mode: from-only constructor, would otherwise run forever
    ESSConsumer<CountingSink> consumer{sink, config, kafka::time::milliseconds{stream.t0_ms - 1'000}};
    consumer.setStopFlag(&stop);
    consumer.run();
    messages = consumer.message_count();
  });
  std::this_thread::sleep_for(std::chrono::seconds(2));
  stop.store(true);
  consumer_thread.join();
  const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
  EXPECT_EQ(messages, stream.n_messages);
  EXPECT_LT(elapsed.count(), 6.0); // ~2 s wait + <=1 s poll latency, with margin
}

}

// explicit main: transitive dependencies drag in Catch2Main, whose main()
// would otherwise win the link over gtest_main's
int main(int argc, char ** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

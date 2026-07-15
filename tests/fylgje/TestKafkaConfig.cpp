// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief Tests for KafkaConfig file parsing: consumer properties pass
///        through, producer-only properties are dropped
//===----------------------------------------------------------------------===//
#include <filesystem>
#include <fstream>
#include <fmt/format.h>
#include <gtest/gtest.h>
#include <unistd.h>
#include "KafkaConfig.h"

namespace {

class KafkaConfigTest : public ::testing::Test {
protected:
  std::filesystem::path config_path;

  void SetUp() override {
    const auto * info = ::testing::UnitTest::GetInstance()->current_test_info();
    config_path = std::filesystem::temp_directory_path()
        / fmt::format("fylgje-{}-{}.json", info->name(), getpid());
  }
  void TearDown() override {
    std::filesystem::remove(config_path);
  }

  void write_config(const std::string & content) {
    std::ofstream out(config_path);
    out << content;
  }
};

TEST_F(KafkaConfigTest, EmptyPathYieldsNoParms) {
  const KafkaConfig config{""};
  EXPECT_TRUE(config.CfgParms.empty());
}

TEST_F(KafkaConfigTest, KeepsConsumerProperties) {
  write_config(R"({"KafkaParms": [
    {"fetch.message.max.bytes": "10485760"},
    {"enable.auto.commit": "false"}
  ]})");
  const KafkaConfig config{config_path.string()};
  ASSERT_EQ(config.CfgParms.size(), 2u);
  EXPECT_EQ(config.CfgParms[0], (std::pair<std::string, std::string>{"fetch.message.max.bytes", "10485760"}));
  EXPECT_EQ(config.CfgParms[1], (std::pair<std::string, std::string>{"enable.auto.commit", "false"}));
}

TEST_F(KafkaConfigTest, DropsProducerOnlyProperties) {
  // the ISSUES.md CONFWARN: queue.buffering.max.ms in a consumer's KafkaParms
  write_config(R"({"KafkaParms": [
    {"queue.buffering.max.ms": "50"},
    {"fetch.message.max.bytes": "10485760"},
    {"linger.ms": "5"},
    {"acks": "all"}
  ]})");
  const KafkaConfig config{config_path.string()};
  ASSERT_EQ(config.CfgParms.size(), 1u);
  EXPECT_EQ(config.CfgParms[0].first, "fetch.message.max.bytes");
}

TEST_F(KafkaConfigTest, InvalidFileThrows) {
  write_config("not json at all");
  // the parse failure surfaces before the constructor's own try block, so
  // only the fact of throwing is contractual, not the exception type
  EXPECT_ANY_THROW(KafkaConfig{config_path.string()});
}

}

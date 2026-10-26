// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief Tests for AsyncWriter: batches written on the background thread
///        land complete and in order, remain SWMR-visible, and write
///        failures surface through finish() and the stop flag
//===----------------------------------------------------------------------===//
#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>
#include <gtest/gtest.h>
#include <highfive/highfive.hpp>

#include "AsyncWriter.h"
#include "Calibration.h"
#include "EventManager.h"
#include "PixelManager.h"

namespace {

using bifrost::data::AsyncWriter;
using bifrost::data::EventManager;
using bifrost::data::PixelManager;

constexpr int arcs{5}, triplets{9}, tubes{3}, pixelation{100};

class AsyncWriterTest : public ::testing::Test {
protected:
  Calibration calibration; // referenced by the managers: must outlive them
  std::unique_ptr<EventManager> data;
  std::filesystem::path path;

  void SetUp() override {
    data = std::make_unique<EventManager>(PixelManager(arcs, triplets, tubes, pixelation, calibration));
    data->storeEvents(true);
    const auto * info = ::testing::UnitTest::GetInstance()->current_test_info();
    path = std::filesystem::path(::testing::TempDir()) /
           (std::string{"fylgje-async-"} + info->name() + ".h5");
    std::filesystem::remove(path);
  }

  void TearDown() override { std::filesystem::remove(path); }

  /// messages with recognizable per-index fields; fiber in [0,6), group in [0,15)
  void add_messages(const int count, const int first_index) const {
    for (int i = 0; i < count; ++i) {
      const auto n = first_index + i;
      data->add(n % 6, n % 15, 100 + n, 200 + n, 0.1 * n,
                static_cast<uint32_t>(n), static_cast<uint32_t>(n + 1));
    }
  }

  [[nodiscard]] static HighFive::DataSet messages_dataset(const HighFive::File & file) {
    return file.getGroup("fylgje").getGroup("events").getDataSet("messages");
  }

  [[nodiscard]] static size_t extent(const HighFive::DataSet & dataset) {
    return dataset.getDimensions().at(0);
  }

  /// wait (bounded) for the writer to have completed at least n batch writes
  static void wait_for_flushes(const AsyncWriter & writer, const int64_t n) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (writer.flushes() < n && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    ASSERT_GE(writer.flushes(), n);
  }
};

/// The producing thread keeps accumulating and handing over batches while the
/// writer works (backpressure cap 2 guarantees genuine overlap); every message
/// must land in the file exactly once, in hand-off order
TEST_F(AsyncWriterTest, BatchesLandCompleteAndInOrder) {
  data->open_file(path, std::nullopt, true);
  constexpr int batches{20}, per_batch{500};
  {
    AsyncWriter writer{*data, nullptr, 2};
    for (int b = 0; b < batches; ++b) {
      add_messages(per_batch, b * per_batch);
      writer.enqueue({data->take_messages(), {}});
    }
    writer.finish();
    EXPECT_EQ(writer.flushes(), batches);
    EXPECT_EQ(writer.pending_batches(), 0u);
    EXPECT_GT(writer.write_duration().count(), 0);
  }
  data->close_file();

  const HighFive::File file(path, HighFive::File::ReadOnly);
  auto dataset = messages_dataset(file);
  constexpr auto total = static_cast<size_t>(batches) * per_batch;
  ASSERT_EQ(extent(dataset), total);
  std::vector<bifrost::message_t> readback(total);
  dataset.read(readback);
  for (size_t n = 0; n < total; ++n) {
    ASSERT_EQ(readback[n].a, static_cast<int>(100 + n)) << "message " << n;
    ASSERT_EQ(readback[n].fiber, static_cast<int>(n % 6)) << "message " << n;
  }
}

/// Async flushes stay visible to an SWMR reader, like synchronous ones
TEST_F(AsyncWriterTest, SwmrReaderFollowsAsyncGrowth) {
  data->open_file(path, std::nullopt, true);
  AsyncWriter writer{*data, nullptr, 2};

  add_messages(10, 0);
  writer.enqueue({data->take_messages(), {}});
  wait_for_flushes(writer, 1);

  {
    // the reader's handles close at the end of this scope
    const HighFive::File reader(path, HighFive::File::ReadOnly | HighFive::File::ReadSWMR);
    auto dataset = messages_dataset(reader);
    EXPECT_EQ(extent(dataset), 10u);

    add_messages(10, 10);
    writer.enqueue({data->take_messages(), {}});
    wait_for_flushes(writer, 2);
    dataset.refresh();
    EXPECT_EQ(extent(dataset), 20u);
  }
  writer.finish();
  data->close_file();
}

/// A write failure on the background thread raises the stop flag promptly and
/// is rethrown by finish(); finish() is idempotent afterwards
TEST_F(AsyncWriterTest, WriteFailureSurfaces) {
  data->open_file(path);
  add_messages(10, 0);
  auto batch = data->take_messages();
  data->close_file(); // writing the batch must now fail: no open file

  std::atomic<bool> stop{false};
  AsyncWriter writer{*data, &stop, 2};
  writer.enqueue({std::move(batch), {}});
  EXPECT_THROW(writer.finish(), std::runtime_error);
  EXPECT_TRUE(stop.load());
  EXPECT_NO_THROW(writer.finish()); // errors are reported once
}

}

// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief Tests for EventManager incremental HDF5 writing (open_file/flush/
///        close_file) and its SWMR readability
//===----------------------------------------------------------------------===//
#include <filesystem>
#include <gtest/gtest.h>
#include <highfive/highfive.hpp>

#include "Calibration.h"
#include "EventManager.h"
#include "HistogramManager.h"
#include "PixelManager.h"
#include "Version.h"

namespace {

using bifrost::data::EventManager;
using bifrost::data::HistogramManager;
using bifrost::data::PixelManager;

constexpr int arcs{5}, triplets{9}, tubes{3}, pixelation{100};

class EventManagerWriterTest : public ::testing::Test {
protected:
  Calibration calibration; // referenced by the managers: must outlive them
  std::unique_ptr<EventManager> data;
  std::filesystem::path path;

  void SetUp() override {
    data = std::make_unique<EventManager>(
        PixelManager(arcs, triplets, tubes, pixelation, calibration),
        HistogramManager(arcs, triplets, calibration));
    data->storeEvents(true).storePixels(true);
    const auto * info = ::testing::UnitTest::GetInstance()->current_test_info();
    path = std::filesystem::path(::testing::TempDir()) /
           (std::string{"fylgje-writer-"} + info->name() + ".h5");
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
};

TEST_F(EventManagerWriterTest, AppendsAcrossFlushes) {
  constexpr int first{100}, second{50};
  add_messages(first, 0);
  data->open_file(path, std::nullopt, true);
  data->flush();
  add_messages(second, first);
  data->close_file(); // final flush included

  const HighFive::File file(path, HighFive::File::ReadOnly);
  auto dataset = messages_dataset(file);
  ASSERT_EQ(extent(dataset), first + second);

  // field values survive the append boundary intact
  std::vector<bifrost::message_t> readback(first + second);
  dataset.read(readback);
  for (int n: {0, first - 1, first, first + second - 1}) {
    EXPECT_EQ(readback[n].fiber, n % 6) << "message " << n;
    EXPECT_EQ(readback[n].a, 100 + n) << "message " << n;
    EXPECT_EQ(readback[n].b, 200 + n) << "message " << n;
    EXPECT_DOUBLE_EQ(readback[n].time, 0.1 * n) << "message " << n;
  }

  // the version attribute comes from the shared fylgje_core definition
  for (const auto & group_name: {"events", "histograms", "pixels"}) {
    const auto version = file.getGroup("fylgje").getGroup(group_name).getAttribute("version").read<std::string>();
    EXPECT_EQ(version, fylgje::version) << group_name;
  }

  // histogram and pixel trees were created and written
  auto root = file.getGroup("fylgje");
  EXPECT_TRUE(root.exist("histograms"));
  ASSERT_TRUE(root.exist("pixels"));
  auto pixels = root.getGroup("pixels").getDataSet("data");
  EXPECT_EQ(extent(pixels), static_cast<size_t>(arcs * triplets * tubes * pixelation));
}

TEST_F(EventManagerWriterTest, SwmrReaderFollowsGrowth) {
  add_messages(10, 0);
  data->open_file(path, std::nullopt, true);
  data->flush();

  // a second handle (stand-in for another process) reads while we write
  {
    // the reader's handles close at the end of this scope
    const HighFive::File reader(path, HighFive::File::ReadOnly | HighFive::File::ReadSWMR);
    auto dataset = messages_dataset(reader);
    EXPECT_EQ(extent(dataset), 10u);

    add_messages(10, 10);
    data->flush();
    dataset.refresh();
    EXPECT_EQ(extent(dataset), 20u);
  }
  data->close_file();
}

TEST_F(EventManagerWriterTest, RefusesExistingGroup) {
  data->open_file(path);
  data->close_file();
  // the file now holds /fylgje: the writer must refuse it
  EXPECT_THROW(data->open_file(path), std::runtime_error);
}

}

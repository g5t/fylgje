// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief Tests for EventManager incremental HDF5 writing (open_file/flush/
///        close_file) and its SWMR readability, plus the one-shot save_to
///        contract the GUI relies on
//===----------------------------------------------------------------------===//
#include <filesystem>
#include <gtest/gtest.h>
#include <h5cpp/hdf5.hpp>

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

  [[nodiscard]] static hdf5::node::Dataset messages_dataset(const hdf5::file::File & file) {
    return file.root().get_group("fylgje").get_group("events").get_dataset("messages");
  }

  [[nodiscard]] static size_t extent(const hdf5::node::Dataset & dataset) {
    const auto simple = hdf5::dataspace::Simple(dataset.dataspace());
    return simple.current_dimensions().at(0);
  }
};

TEST_F(EventManagerWriterTest, AppendsAcrossFlushes) {
  constexpr int first{100}, second{50};
  add_messages(first, 0);
  data->open_file(path, std::nullopt, true);
  data->flush();
  add_messages(second, first);
  data->close_file(); // final flush included

  auto file = hdf5::file::open(std::string(path), hdf5::file::AccessFlags::ReadOnly);
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
    std::string version;
    file.root().get_group("fylgje").get_group(group_name).attributes["version"].read(version);
    EXPECT_EQ(version, fylgje::version) << group_name;
  }

  // histogram and pixel trees were created and written
  auto root = file.root().get_group("fylgje");
  EXPECT_TRUE(root.has_group("histograms"));
  ASSERT_TRUE(root.has_group("pixels"));
  auto pixels = root.get_group("pixels").get_dataset("data");
  EXPECT_EQ(extent(pixels), static_cast<size_t>(arcs * triplets * tubes * pixelation));
}

TEST_F(EventManagerWriterTest, SwmrReaderFollowsGrowth) {
  add_messages(10, 0);
  data->open_file(path, std::nullopt, true);
  data->flush();

  // a second handle (stand-in for another process) reads while we write
  auto reader = hdf5::file::open(std::string(path),
                                 hdf5::file::AccessFlags::ReadOnly | hdf5::file::AccessFlags::SWMRRead);
  auto dataset = messages_dataset(reader);
  EXPECT_EQ(extent(dataset), 10u);

  add_messages(10, 10);
  data->flush();
  dataset.refresh();
  EXPECT_EQ(extent(dataset), 20u);

  reader.close();
  data->close_file();
}

TEST_F(EventManagerWriterTest, RefusesExistingGroup) {
  data->open_file(path);
  data->close_file();
  // the file now holds /fylgje: both writer and one-shot saves must refuse it
  EXPECT_THROW(data->open_file(path), std::runtime_error);
  EXPECT_THROW(data->save_to(path), std::runtime_error);
}

TEST_F(EventManagerWriterTest, OneShotSaveKeepsEventsInMemory) {
  // the GUI saves via save_to and may save repeatedly: the second file must
  // still contain everything, i.e. save_to must not drain the event list
  constexpr int count{25};
  add_messages(count, 0);
  const auto second_path = path.string() + ".again.h5";
  data->save_to(path);
  data->save_to(second_path);

  for (const auto & p: {path.string(), second_path}) {
    auto file = hdf5::file::open(p, hdf5::file::AccessFlags::ReadOnly);
    EXPECT_EQ(extent(messages_dataset(file)), count) << p;
  }
  std::filesystem::remove(second_path);
}

}

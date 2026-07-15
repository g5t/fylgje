// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief fylgje interface to handle events from AR51 format messages
//===----------------------------------------------------------------------===//
#pragma once
#include <map>
#include <vector>
#include <fmt/format.h>
#include <h5cpp/hdf5.hpp>
#include "Message.h"
#include "PixelManager.h"
#include "HistogramManager.h"

namespace bifrost::data {
  class EventManager {
  public:
    using message_t = bifrost::message_t;
  protected:
    std::vector<message_t> messages;
    PixelManager pixel_manager;
    std::optional<HistogramManager> histogram_manager;
    bool store_pixels{false};
    bool store_events{false};
    // incremental-writing state (see open_file/flush/close_file)
    std::optional<hdf5::file::File> out_file;
    std::optional<hdf5::node::Group> out_group;
    std::optional<hdf5::node::Dataset> out_messages;
    size_t messages_written{0};

  public:
    EventManager(PixelManager && pixelManager, HistogramManager && histogramManager)
      : pixel_manager{std::move(pixelManager)}, histogram_manager{std::move(histogramManager)} {}
    EventManager(const PixelManager & pixelManager, const HistogramManager & histogramManager)
      : pixel_manager{pixelManager}, histogram_manager{histogramManager} {}

    explicit EventManager(PixelManager && pixelManager) : pixel_manager{std::move(pixelManager)} {}
    explicit EventManager(const PixelManager & pixelManager) : pixel_manager{pixelManager} {}

    // / \brief Add a message to the event manager from a message_t
    bool add(const message_t & message);
    // / \brief Add a message to the event manager from its components
    bool add(int fiber, int group, int a, int b, double time, uint32_t high, uint32_t low);

    EventManager & storePixels(bool store=true) {
      store_pixels = store;
      return *this;
    }
    EventManager & storeEvents(bool store=true) {
      store_events = store;
      return *this;
    }

    void clear() {
      pixel_manager.clear();
      if (histogram_manager.has_value()){
        histogram_manager->clear();
      }
    }

    const HistogramManager & histograms() const {
      if (histogram_manager.has_value()){
        return *histogram_manager;
      }
      throw std::runtime_error("No histogram manager available");
    }
    HistogramManager & histograms() {
      if (histogram_manager.has_value()){
        return *histogram_manager;
      }
      throw std::runtime_error("No histogram manager available");
    }

    const PixelManager & pixels() const { return pixel_manager; }

    void save_to(const hdf5::node::Group & group) const;
    void save_to(const hdf5::file::File & file, const std::optional<std::string> & group = std::nullopt) const;
    void save_to(const std::filesystem::path & file, const std::optional<std::string> & group = std::nullopt) const;

    // Incremental writing: open_file creates the complete file structure up
    // front (events dataset chunked and unlimited), flush appends collected
    // events (clearing them from memory) and rewrites histograms/pixels in
    // place, close_file flushes and releases the file. With swmr=true the
    // file is reopened in HDF5 Single-Writer/Multiple-Reader mode after
    // creation, so other processes can read it (SWMRRead + refresh) while it
    // is written; this forces the latest HDF5 file format. Not thread-safe:
    // call add/flush/close_file from one thread.
    void open_file(const std::filesystem::path & file, const std::optional<std::string> & group = std::nullopt, bool swmr = false);
    void flush();
    void close_file();
    [[nodiscard]] bool file_is_open() const { return out_file.has_value(); }
  };
}
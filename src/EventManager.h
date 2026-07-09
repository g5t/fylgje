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
  };
}
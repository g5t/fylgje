// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief GUI fylgje application interface to handle AR51 format messages
/// \note This light wrapper is need since the held HistogramManager is
///       specialized to use QCustomPlot
//===----------------------------------------------------------------------===//
#pragma once
#include "EventManager.h"
#include "QHistogramManager.h"

namespace bifrost::data::Q {
class EventManager : public bifrost::data::EventManager {

public:
  EventManager(PixelManager && pixelManager, HistogramManager && histogramManager)
      : bifrost::data::EventManager(std::move(pixelManager), std::move(histogramManager)) {}
  EventManager(const PixelManager & pixelManager, const HistogramManager & histogramManager)
      : bifrost::data::EventManager(pixelManager, histogramManager) {}

  explicit EventManager(PixelManager && pixelManager) : bifrost::data::EventManager(std::move(pixelManager)) {}
  explicit EventManager(const PixelManager & pixelManager) : bifrost::data::EventManager(pixelManager) {}

  bifrost::data::Q::HistogramManager & qHistograms() {
    if (histogram_manager.has_value()){
      return static_cast<HistogramManager &>(*histogram_manager);
    }
    throw std::runtime_error("No histogram manager available");
  }
};
}
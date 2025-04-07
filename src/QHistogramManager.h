// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief GUI fylgje application interface to handle binning data from AR51 format messages
/// \note This wrapper is need since it provides QCustomPlot-encapsulated data for plotting
//===----------------------------------------------------------------------===//
#pragma once
#include <QVector>
#include <QPlot/qcustomplot/qcustomplot.h>
#include "HistogramManager.h"

namespace bifrost::data::Q {
  ///\brief HistogramManager is a wrapper around bifrost::data::HistogramManager
  ///\details This class is used to manage interface for QCustomPlot
  class HistogramManager: public bifrost::data::HistogramManager {
  public:
    using D1 = std::vector<double>;
    using D2 = QCPColorMapData;

    HistogramManager(int arcs, int triplets, Calibration & calib)
        : bifrost::data::HistogramManager(arcs, triplets, calib) {}

    [[nodiscard]] double max(Filter) const;
    [[nodiscard]] double max(int arc, Filter) const;
    [[nodiscard]] double max(int arc, int triplet, Filter) const;
    [[nodiscard]] double max(int arc, Type t, Filter) const;
    [[nodiscard]] double max(int arc, int triplet, Type t, Filter) const;
    [[nodiscard]] double max(key_t k, Filter) const;

    [[nodiscard]] double min(Filter) const;
    [[nodiscard]] double min(int arc, Filter) const;
    [[nodiscard]] double min(int arc, int triplet, Filter) const;
    [[nodiscard]] double min(int arc, Type t, Filter) const;
    [[nodiscard]] double min(int arc, int triplet, Type t, Filter) const;
    [[nodiscard]] double min(key_t k, Filter) const;

    [[nodiscard]] D1 data_1D(int arc, int triplet, Type t, Filter) const;
    [[nodiscard]] D1 data_1D(key_t k, Filter) const;
    [[nodiscard]] D2 * data_2D(int arc, int triplet, Type t, Filter) const;
    [[nodiscard]] D2 * data_2D(key_t k, Filter) const;
  private:
    [[nodiscard]] int max_1D(key_t t, Filter) const;
    [[nodiscard]] int max_2D(key_t t, Filter) const;
    [[nodiscard]] int min_1D(key_t t, Filter) const;
    [[nodiscard]] int min_2D(key_t t, Filter) const;
    [[nodiscard]] std::pair<int, int> bins_2D(Type t) const;
  };
}
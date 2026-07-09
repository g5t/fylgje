// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief Interface between DataManager and QCustomPlot for plotting
//===----------------------------------------------------------------------===//

#pragma once
#include <map>
#include <QGridLayout>
#include <QPlot/qcustomplot/qcustomplot.h>
#include <fmt/format.h>

#include "HistogramManager.h"

#include <iostream>

/// \brief Create a QCPColorGradient from a colormap name string
QCPColorGradient named_colormap(std::string_view name, bool invert);

class PlotManager{
public:
  using type_t = ::bifrost::data::Type;
  using layout_t = QGridLayout;
  enum class Dim {none, one, two};

  /// Callback type for mouse-press and mouse-move events on individual plots
  using mouse_cb_t = std::function<void(int i, int j, QMouseEvent *)>;
  /// Callback type for double-click events (receives cell coords only)
  using dbl_cb_t   = std::function<void(int i, int j)>;

  PlotManager(layout_t * l, int n1, int n2): layout(l), n1(n1), n2(n2) {
      dims[0] = Dim::none;
  }

  /// \brief (Re)set the plot layout to a single 1-D or 2-D plot
  void make_single(Dim d, type_t t);

  /// \brief (Re)set the plot layout to 9 identical 1-D or 2-D plots
  void make_all_same(Dim d, type_t t);

  /// \brief (Re)set the plot layout to 9 different 1-D and 2-D plots
  void make_multi(std::array<type_t, 9> ts);

  /// \brief Plot a 1D histogram convenience function converting to QVectors
  void plot(int i, int j, const std::vector<double> & x, const std::vector<double> & y, double min, double max, bool is_log);

  /// \brief Plot a 1D histogram
  void plot(int i, int j, const QVector<double> * x, const QVector<double> * y, double min, double max, bool is_log);

  /// \brief Plot multiple 1D histograms on the same plot
  void plot_all_included_excluded(int i, int j, const std::vector<double> & std_x,
                                  const std::optional<std::vector<double>> & all,
                                  const std::optional<std::vector<double>> & included,
                                  const std::optional<std::vector<double>> & excluded,
                                  double min, double max, bool is_log);


  /// \brief Plot a 2D histogram
  void plot(int i, int j, QCPColorMapData * data, double min, double max, bool is_log,
            std::string_view gradient, bool is_inverted,
            const std::optional<std::vector<std::pair<double, double>>> & left,
            const std::optional<std::vector<std::pair<double, double>>> & center,
            const std::optional<std::vector<std::pair<double, double>>> & right);

  // ── Mouse-interaction hooks ──────────────────────────────────────────────
  void set_click_callback(mouse_cb_t cb)       { click_callback = std::move(cb); }
  void set_hover_callback(mouse_cb_t cb)       { hover_callback = std::move(cb); }
  void set_double_click_callback(dbl_cb_t cb)  { double_click_callback = std::move(cb); }

  /// \brief Clear the user-zoom flag for (i,j) so the next plot() call resets its range
  void reset_zoom(int i, int j);

  // ── Per-cell accessors used by click/hover handlers in MainWindow ────────
  [[nodiscard]] Dim          dim(int i, int j)      const;
  [[nodiscard]] type_t       type_at(int i, int j)  const;
  [[nodiscard]] bool         is_flipped(int i, int j) const;
  [[nodiscard]] QCustomPlot* plot_at(int i, int j)  const;
  [[nodiscard]] QCPColorMap* image_at(int i, int j) const;


private:
  [[nodiscard]] inline int key(int i, int j, ::bifrost::data::Filter filter = ::bifrost::data::Filter::none) const {
      using ::bifrost::data::Filter;
      auto index = static_cast<int>(filter);
      return i + n1 * j + n1 * n2 * index;
    }
    void make_plot(int i, int j, bool flip, type_t t);
    void make_1D(int i, int j, bool flip, type_t t);
    void make_2D(int i, int j, bool flip, type_t t);
    void set_axis_labels(int i, int j);

private:
  layout_t * layout{};
  int n1;
  int n2;

  std::map<int, QCustomPlot *> plots;
  std::map<int, QCPColorMap *> images;
  std::map<int, QCPGraph *> lines;
  std::map<int, QCPCurve *> polygons;
  std::map<int, Dim> dims;
  std::map<int, type_t> types;
  std::map<int, bool> flipped;

  // Zoom-persistence state
  std::map<int, bool> user_zoomed;
  bool in_range_update{false};

  // Mouse-interaction callbacks
  mouse_cb_t click_callback;
  mouse_cb_t hover_callback;
  dbl_cb_t   double_click_callback;

  void clear(){
    qDeleteAll(layout->children());
    plots.clear();
    images.clear();
    lines.clear();
    dims.clear();
    types.clear();
    flipped.clear();
    user_zoomed.clear();
    polygons.clear();
  }

  void remove(int i, int j){
    auto item = layout->itemAtPosition(i, j);
    if (item){
      layout->removeItem(item);
      delete item->widget();
      delete item;
    }
    auto k = key(i, j);
    if (plots.count(k)) plots.erase(k);
    if (images.count(k)) images.erase(k);
    if (lines.count(k)) lines.erase(k);
    if (dims.count(k)) dims.erase(k);
    if (types.count(k)) types.erase(k);
    if (flipped.count(k)) flipped.erase(k);
    if (user_zoomed.count(k)) user_zoomed.erase(k);
  }

  void empty_layout(){
    for (int i=0; i<layout->rowCount(); ++i){
      for (int j=0; j<layout->columnCount(); ++j){
        remove(i, j);
      }
    }
  }
};
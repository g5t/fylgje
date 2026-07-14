// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
//
/// \file
//
/// \brief Implementation of plot interface for Fylgje
//===----------------------------------------------------------------------===//
#include "PlotManager.h"
#include "HistogramManager.h"

// ── make_single / make_all_same / make_multi ─────────────────────────────────

void PlotManager::make_single(const Dim d, const type_t t){
  if (layout->rowCount() != 1 || layout->columnCount() != 1 || d != dims[0]){
    empty_layout();
    if (Dim::one == d) make_1D(0, 0, false, t);
    if (Dim::two == d) make_2D(0, 0, false, t);
  }
  // In the single-plot view give axes full room to show their labels
  if (const auto k = key(0, 0); plots.count(k)){
    const bool type_changed = !types.count(k) || types.at(k) != t;
    types[k] = t;
    if (type_changed) user_zoomed.erase(k);   // new type → new data, reset zoom
    set_axis_labels(0, 0);
    // plots[k]->axisRect()->setAutoMargins(QCP::msAll);
  }
}

void PlotManager::make_all_same(const Dim d, const type_t t){
  for (int i=0; i<3; ++i) {
    for (int j=0; j<3; ++j) {
      const auto k = key(i, j);
      const bool exists = layout->itemAtPosition(i, j) != nullptr;
      // Recreate only when the dimensionality changes
      if (exists && d != dims[k]) remove(i, j);
      if (!layout->itemAtPosition(i, j)) {
        (Dim::one == d) ? make_1D(i, j, false, t) : make_2D(i, j, false, t);
      } else if (exists && types.count(k) && types.at(k) != t) {
        // Same dim but different type: new data → reset zoom and refresh labels
        types[k] = t;
        user_zoomed.erase(k);
        set_axis_labels(i, j);
      }
    }
  }
}

void PlotManager::make_multi(const std::array<type_t, 9> &ts){
  for (int i=0; i<3; ++i) {
    for (int j=0; j<3; ++j) {
      const auto k = key(i, j);
      const Dim target_dim{i == 0 || j > 1 ? Dim::one : Dim::two};
      const bool flip = (i>0) & (j>1);
      const bool exists = layout->itemAtPosition(i, j) != nullptr;
      const bool wrong_dim  = exists && target_dim != dims[k];
      const bool wrong_flip = exists && !wrong_dim && flipped.count(k) && flipped.at(k) != flip;
      // Recreate when dim or flip changes
      if (wrong_dim || wrong_flip) remove(i, j);
      if (!layout->itemAtPosition(i, j)) {
        (Dim::one == target_dim) ? make_1D(i, j, flip, ts[i*3+j]) : make_2D(i, j, false, ts[i*3+j]);
      } else if (exists && types.count(k) && types.at(k) != ts[i*3+j]) {
        // Same dim/flip but different type: reset zoom and refresh labels
        types[k] = ts[i*3+j];
        user_zoomed.erase(k);
        set_axis_labels(i, j);
      }
    }
  }
}

// ── plot() overloads ──────────────────────────────────────────────────────────

void PlotManager::plot(const int i, const int j, const std::vector<double> & x, const std::vector<double> & y, const double min, const double max, const bool is_log){
  const QVector<double> q_x(x.begin(), x.end());
  const QVector<double> q_y(y.begin(), y.end());
  plot(i, j, &q_x, &q_y, min, max, is_log);
}

void PlotManager::plot(const int i, const int j, const QVector<double> * x, const QVector<double> * y, const double min, const double max, const bool is_log){
  const auto k = key(i, j);
  if (!dims.count(k) || dims.at(k) != Dim::one) return;
  const auto g = lines.at(k);
  g->setData(*x, *y);
  const auto p = plots.at(k);
  bool updating_ranges{false};
  if (!user_zoomed.count(k) || !user_zoomed.at(k)) {
    updating_ranges = true;
    QSignalBlocker block_x(p->xAxis);
    QSignalBlocker block_y(p->yAxis);
    in_range_update = true;
    QCPAxis * independent{flipped[k] ? p->yAxis : p->xAxis};
    independent->setRange(x->front(), x->back());
    QCPAxis * ax{flipped[k] ? p->xAxis : p->yAxis};
    ax->setScaleType(is_log ? QCPAxis::stLogarithmic : QCPAxis::stLinear);
    ax->setRange(min - (max - min) / 40, max + (max - min) / 20);
  }
  p->replot();
  if (updating_ranges) in_range_update = false;
}

void PlotManager::plot_all_included_excluded(const int i, const int j, const std::vector<double> & std_x,
                                const std::optional<std::vector<double>> & all,
                                const std::optional<std::vector<double>> & included,
                                const std::optional<std::vector<double>> & excluded,
                                const double min, const double max, const bool is_log) {
  using ::bifrost::data::Filter;
  const auto k = key(i, j);
  if (!dims.count(k) || dims.at(k) != Dim::one) return;

  const QVector<double> x(std_x.begin(), std_x.end());
  if (all.has_value()) {
    lines.at(key(i, j, Filter::none))->setData(x, QVector<double>(all.value().begin(), all.value().end()));
  }
  if (included.has_value()) {
    lines.at(key(i, j, Filter::positive))->setData(x, QVector<double>(included.value().begin(), included.value().end()));
  }
  if (excluded.has_value()) {
    lines.at(key(i, j, Filter::negative))->setData(x, QVector<double>(excluded.value().begin(), excluded.value().end()));
  }

  const auto p = plots.at(k);
  bool updating_ranges{false};
  if (!user_zoomed.count(k) || !user_zoomed.at(k)) {
    updating_ranges = true;
    QSignalBlocker block_x(p->xAxis);
    QSignalBlocker block_y(p->yAxis);
    in_range_update = true;
    QCPAxis * independent{flipped[k] ? p->yAxis : p->xAxis};
    independent->setRange(x.front(), x.back());
    QCPAxis * ax{flipped[k] ? p->xAxis : p->yAxis};
    ax->setScaleType(is_log ? QCPAxis::stLogarithmic : QCPAxis::stLinear);
    ax->setRange(min - (max - min) / 40, max + (max - min) / 20);
  }
  p->replot();
  if (updating_ranges) in_range_update = false;
}

void PlotManager::plot(const int i, const int j, QCPColorMapData * data, const double min, const double max, const bool is_log,
          const std::string_view gradient, const bool is_inverted,
          const std::optional<std::vector<std::pair<double, double>>> & left,
          const std::optional<std::vector<std::pair<double, double>>> & center,
          const std::optional<std::vector<std::pair<double, double>>> & right){
  const auto k = key(i, j);
  if (!dims.count(k) || dims.at(k) != Dim::two) return;
  if (!images.count(k)) return;
  const auto im = images.at(k);
  im->setData(data);
  const auto p = plots.at(k);
  bool updating_ranges{false};
  if (!user_zoomed.count(k) || !user_zoomed.at(k)) {
    updating_ranges = true;
    QSignalBlocker block_x(p->xAxis);
    QSignalBlocker block_y(p->yAxis);
    in_range_update = true;
    p->xAxis->setRange(0, ::bifrost::data::BIN2D);
    p->yAxis->setRange(0, ::bifrost::data::BIN2D);
  }
  im->setGradient(named_colormap(gradient, is_inverted));
  im->setDataScaleType(is_log ? QCPAxis::stLogarithmic : QCPAxis::stLinear);
  im->setDataRange(QCPRange(min, max));

  auto poly = [](const std::vector<std::pair<double, double>> & v){
    std::vector<double> x, y;
    x.reserve(v.size()+1);
    y.reserve(v.size()+1);
    for (const auto & [a, b]: v){
      x.push_back(a);
      y.push_back(b);
    }
    x.push_back(v.front().first);
    y.push_back(v.front().second);
    QVector<double> q_x(x.begin(), x.end());
    QVector<double> q_y(y.begin(), y.end());
    return std::make_pair(q_x, q_y);
  };
  using ::bifrost::data::Filter;
  if (left.has_value()){
    const auto [x, y] = poly(left.value());
    polygons[key(i, j, Filter::negative)]->addData(x, y);
  }
  if (center.has_value()){
    const auto [x, y] = poly(center.value());
    polygons[key(i, j, Filter::none)]->addData(x, y);
  }
  if (right.has_value()){
    const auto [x, y] = poly(right.value());
    polygons[key(i, j, Filter::positive)]->addData(x, y);
  }
  p->replot();
  if (updating_ranges) in_range_update = false;
}

// ── Zoom reset ────────────────────────────────────────────────────────────────

void PlotManager::reset_zoom(const int i, const int j) {
  const auto k = key(i, j);
  user_zoomed.erase(k);
  // 2-D: reset to the full BIN2D range immediately so the user sees the change at once
  if (dims.count(k) && dims.at(k) == Dim::two && plots.count(k)) {
    QSignalBlocker block_x(plots[k]->xAxis);
    QSignalBlocker block_y(plots[k]->yAxis);
    in_range_update = true;
    plots[k]->xAxis->setRange(0, ::bifrost::data::BIN2D);
    plots[k]->yAxis->setRange(0, ::bifrost::data::BIN2D);
    in_range_update = false;
  }
  // 1-D ranges are restored on the next timer-driven plot() call
}

// ── Per-cell accessors ────────────────────────────────────────────────────────

PlotManager::Dim PlotManager::dim(const int i, const int j) const {
  const auto k = key(i, j);
  return dims.count(k) ? dims.at(k) : Dim::none;
}
PlotManager::type_t PlotManager::type_at(const int i, const int j) const {
  const auto k = key(i, j);
  return types.count(k) ? types.at(k) : type_t::unknown;
}
bool PlotManager::is_flipped(const int i, const int j) const {
  const auto k = key(i, j);
  return flipped.count(k) && flipped.at(k);
}
QCustomPlot * PlotManager::plot_at(const int i, const int j) const {
  const auto k = key(i, j);
  return plots.count(k) ? plots.at(k) : nullptr;
}
QCPColorMap * PlotManager::image_at(const int i, const int j) const {
  const auto k = key(i, j);
  return images.count(k) ? images.at(k) : nullptr;
}

// ── make_plot / make_1D / make_2D / set_axis_labels ──────────────────────────

void PlotManager::make_plot(const int i, const int j, const bool flip, const type_t t){
  if (const auto item = layout->itemAtPosition(i, j); !item){
    const auto p = new QCustomPlot();
    p->axisRect()->setAutoMargins(QCP::msNone);
    p->xAxis->setTicks(false);
    p->yAxis->setTicks(false);
    p->xAxis->setTickPen(QPen(Qt::NoPen));
    //p->yAxis->setTickPen(QPen(Qt::NoPen));
    layout->addWidget(p, i, j);
    plots[key(i, j)] = p;
    types[key(i, j)] = t;
    flipped[key(i, j)] = flip;

    // Zoom / pan
    p->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);

    // Detect user-initiated zoom: axis range changes that are NOT from our own plot() calls
    auto k_val = key(i, j);
    auto detect = [this, k_val](const QCPRange &){
      if (!in_range_update) user_zoomed[k_val] = true;
    };
    QObject::connect(p->xAxis, QOverload<const QCPRange&>::of(&QCPAxis::rangeChanged), p, detect);
    QObject::connect(p->yAxis, QOverload<const QCPRange&>::of(&QCPAxis::rangeChanged), p, detect);

    // Forward mouse events to the registered callbacks
    QObject::connect(p, &QCustomPlot::mousePress, p, [this, i, j](QMouseEvent * ev){
      if (click_callback) click_callback(i, j, ev);
    });
    QObject::connect(p, &QCustomPlot::mouseMove, p, [this, i, j](QMouseEvent * ev){
      if (hover_callback) hover_callback(i, j, ev);
    });
    QObject::connect(p, &QCustomPlot::mouseDoubleClick, p, [this, i, j](QMouseEvent *){
      if (double_click_callback) double_click_callback(i, j);
    });
  }
}

void PlotManager::make_1D(const int i, const int j, const bool flip, const type_t t){
  using ::bifrost::data::Filter;
  const auto k = key(i, j);
  dims[k] = Dim::one;
  in_range_update = true;   // hold for the entire setup so no initial range change
  make_plot(i, j, flip, t);  // connects rangeChanged AFTER this returns

  plots[k]->yAxis->setTicks(true);
  plots[k]->xAxis->setTicks(true);
  plots[k]->yAxis->setTickLabels(true);
  plots[k]->xAxis->setTickLabels(true);
  plots[k]->axisRect()->setupFullAxesBox();

  std::vector<std::pair<Filter, QColor>> filter_color{
      {{Filter::none, Qt::black}, {Filter::positive, Qt::darkGreen}, {Filter::negative, Qt::darkRed}}
  };
  for (const auto & [filter, color]: filter_color){
    const auto lk = key(i, j, filter);
    lines[lk] = new QCPGraph(flip ? plots[k]->yAxis : plots[k]->xAxis, flip ? plots[k]->xAxis : plots[k]->yAxis);
    lines[lk]->setLineStyle(QCPGraph::LineStyle::lsStepCenter);
    lines[lk]->setPen(QPen(color));
  }
  set_axis_labels(i, j);
  in_range_update = false;
}

void PlotManager::make_2D(const int i, const int j, const bool flip, const type_t t){
  using ::bifrost::data::Filter;
  const auto k = key(i, j);
  dims[k] = Dim::two;
  in_range_update = true;   // hold for the entire setup so no initial range change
  make_plot(i, j, flip, t);
  const auto p = plots[k];
  QSignalBlocker block_x(p->xAxis);
  QSignalBlocker block_y(p->yAxis);
  p->xAxis->setRange(0, n2);
  p->yAxis->setRange(0, n2);
  p->axisRect()->setupFullAxesBox();

  const auto m = new QCPColorMap(flip ? p->yAxis : p->xAxis, flip ? p->xAxis : p->yAxis);
  m->data()->setSize(n2, n2);
  m->data()->setRange(QCPRange(0, n2-1), QCPRange(0, n2-1));
  m->setTightBoundary(false);
  m->setInterpolate(false);

  const auto s = new QCPColorScale(p);
  m->setColorScale(s);   // may emit rangeChanged — safe while in_range_update is true
  m->setGradient(QCPColorGradient::gpGrayscale);
  m->rescaleDataRange();
  images[k] = m;

  std::vector<std::pair<Filter, QColor>> filter_color{
      {{Filter::none, Qt::green}, {Filter::positive, Qt::yellow}, {Filter::negative, Qt::magenta}}
  };
  for (const auto & [filter, color]: filter_color){
    const auto pk = key(i, j, filter);
    polygons[pk] = new QCPCurve(flip ? p->yAxis : p->xAxis, flip ? p->xAxis : p->yAxis);
    polygons[pk]->setLineStyle(QCPCurve::LineStyle::lsLine);
    polygons[pk]->setPen(QPen(color));
  }
  set_axis_labels(i, j);
  in_range_update = false;
}

void PlotManager::set_axis_labels(const int i, const int j) {
  const auto k = key(i, j);
  if (!plots.count(k) || !types.count(k)) return;
  const auto names = ::bifrost::data::axes_names(types.at(k));
  const bool flip = flipped.count(k) && flipped.at(k);
  const auto * p = plots.at(k);
  if (dims.at(k) == Dim::one) {
    const QString var = names.empty() ? "" : QString::fromStdString(names[0]);
    (flip ? p->yAxis : p->xAxis)->setLabel(var);
    (flip ? p->xAxis : p->yAxis)->setLabel("Count");
  } else if (dims.at(k) == Dim::two && names.size() >= 2) {
    // flip: colormap key=yAxis (names[0]), value=xAxis (names[1])
    (flip ? p->yAxis : p->xAxis)->setLabel(QString::fromStdString(names[0]));
    (flip ? p->xAxis : p->yAxis)->setLabel(QString::fromStdString(names[1]));
  }
}

// ── named_colormap ────────────────────────────────────────────────────────────

QCPColorGradient named_colormap(const std::string_view name, const bool invert){
  auto grad = QCPColorGradient();
  auto preset = QCPColorGradient::gpGrayscale;
  if (name == "gray" || name == "grey"){
    preset=QCPColorGradient::gpGrayscale;
  } else if (name == "hot"){
    preset=QCPColorGradient::gpHot;
  } else if (name == "cold"){
    preset=QCPColorGradient::gpCold;
  } else if (name == "night"){
    preset=QCPColorGradient::gpNight;
  } else if (name == "candy") {
    preset=QCPColorGradient::gpCandy;
  } else if (name == "geography") {
    preset=QCPColorGradient::gpGeography;
  } else if (name == "thermal") {
    preset=QCPColorGradient::gpThermal;
  }
  grad.loadPreset(preset);
  if (invert) grad=grad.inverted();
  return grad;
}
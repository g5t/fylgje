// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
//
/// \file
//
/// \brief Implementation of plot interface for Fylgje
//===----------------------------------------------------------------------===//
#include "PlotManager.h"

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

void PlotManager::make_single(Dim d, type_t t){
  if (layout->rowCount() != 1 || layout->columnCount() != 1 || d != dims[0]){
    empty_layout();
    if (Dim::one == d) make_1D(0, 0, false, t);
    if (Dim::two == d) make_2D(0, 0, false, t);
  }
  // In the single-plot view give axes full room to show their labels
  auto k = key(0, 0);
  if (plots.count(k)){
    set_axis_labels(0, 0);
    plots[k]->axisRect()->setAutoMargins(QCP::msAll);
  }
}

void PlotManager::make_all_same(Dim d, type_t t){
  empty_layout();
  for (int i=0; i<3; ++i) {
    for (int j=0; j<3; ++j) {
      if (layout->itemAtPosition(i, j) && d !=dims[key(i, j)]){
        remove(i, j);
      }
      if (!layout->itemAtPosition(i, j)) {
        (Dim::one == d) ? make_1D(i, j, false, t) : make_2D(i, j, false, t);
      }
    }
  }
}

void PlotManager::make_multi(std::array<type_t, 9> ts){
  empty_layout();
  for (int i=0; i<3; ++i) {
    for (int j=0; j<3; ++j) {
      Dim t{i == 0 || j > 1 ? Dim::one : Dim::two};
      if (layout->itemAtPosition(i, j) && t !=dims[key(i, j)]){
        remove(i, j);
      }
      if (!layout->itemAtPosition(i, j)) {
        (Dim::one == t) ? make_1D(i, j, (i>0) & (j>1), ts[i*3 + j]) : make_2D(i, j, false, ts[i*3 + j]);
      }
    }
  }
}

// ── plot() overloads ──────────────────────────────────────────────────────────

void PlotManager::plot(int i, int j, const std::vector<double> & x, const std::vector<double> & y, double min, double max, bool is_log){
  QVector<double> q_x(x.begin(), x.end());
  QVector<double> q_y(y.begin(), y.end());
  plot(i, j, &q_x, &q_y, min, max, is_log);
}

void PlotManager::plot(int i, int j, const QVector<double> * x, const QVector<double> * y, double min, double max, bool is_log){
  auto k = key(i, j);
  if (!dims.count(k) || dims.at(k) != Dim::one) return;
  auto g = lines.at(k);
  g->setData(*x, *y);
  auto p = plots.at(k);
  if (!user_zoomed.count(k) || !user_zoomed.at(k)) {
    in_range_update = true;
    QCPAxis * independent{flipped[k] ? p->yAxis : p->xAxis};
    independent->setRange(x->front(), x->back());
    QCPAxis * ax{flipped[k] ? p->xAxis : p->yAxis};
    ax->setScaleType(is_log ? QCPAxis::stLogarithmic : QCPAxis::stLinear);
    ax->setRange(min - (max - min) / 40, max + (max - min) / 20);
    in_range_update = false;
  }
}

void PlotManager::plot_all_included_excluded(int i, int j, const std::vector<double> & std_x,
                                const std::optional<std::vector<double>> & all,
                                const std::optional<std::vector<double>> & included,
                                const std::optional<std::vector<double>> & excluded,
                                double min, double max, bool is_log) {
  using ::bifrost::data::Filter;
  auto k = key(i, j);
  if (!dims.count(k) || dims.at(k) != Dim::one) return;

  QVector<double> x(std_x.begin(), std_x.end());
  if (all.has_value()) {
    lines.at(key(i, j, Filter::none))->setData(x, QVector<double>(all.value().begin(), all.value().end()));
  }
  if (included.has_value()) {
    lines.at(key(i, j, Filter::positive))->setData(x, QVector<double>(included.value().begin(), included.value().end()));
  }
  if (excluded.has_value()) {
    lines.at(key(i, j, Filter::negative))->setData(x, QVector<double>(excluded.value().begin(), excluded.value().end()));
  }

  auto p = plots.at(k);
  if (!user_zoomed.count(k) || !user_zoomed.at(k)) {
    in_range_update = true;
    QCPAxis * independent{flipped[k] ? p->yAxis : p->xAxis};
    independent->setRange(x.front(), x.back());
    QCPAxis * ax{flipped[k] ? p->xAxis : p->yAxis};
    ax->setScaleType(is_log ? QCPAxis::stLogarithmic : QCPAxis::stLinear);
    ax->setRange(min - (max - min) / 40, max + (max - min) / 20);
    in_range_update = false;
  }
}

void PlotManager::plot(int i, int j, QCPColorMapData * data, double min, double max, bool is_log,
          std::string_view gradient, bool is_inverted,
          const std::optional<std::vector<std::pair<double, double>>> & left,
          const std::optional<std::vector<std::pair<double, double>>> & center,
          const std::optional<std::vector<std::pair<double, double>>> & right){
  auto k = key(i, j);
  if (!dims.count(k) || dims.at(k) != Dim::two) return;
  if (!images.count(k)) return;
  auto im = images.at(k);
  im->setData(data);
  auto p = plots.at(k);
  if (!user_zoomed.count(k) || !user_zoomed.at(k)) {
    in_range_update = true;
    p->xAxis->setRange(0, ::bifrost::data::BIN2D);
    p->yAxis->setRange(0, ::bifrost::data::BIN2D);
    in_range_update = false;
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
    auto [x, y] = poly(left.value());
    polygons[key(i, j, Filter::negative)]->addData(x, y);
  }
  if (center.has_value()){
    auto [x, y] = poly(center.value());
    polygons[key(i, j, Filter::none)]->addData(x, y);
  }
  if (right.has_value()){
    auto [x, y] = poly(right.value());
    polygons[key(i, j, Filter::positive)]->addData(x, y);
  }
}

// ── Zoom reset ────────────────────────────────────────────────────────────────

void PlotManager::reset_zoom(int i, int j) {
  auto k = key(i, j);
  user_zoomed.erase(k);
  // 2-D: reset to the full BIN2D range immediately so the user sees the change at once
  if (dims.count(k) && dims.at(k) == Dim::two && plots.count(k)) {
    in_range_update = true;
    plots[k]->xAxis->setRange(0, ::bifrost::data::BIN2D);
    plots[k]->yAxis->setRange(0, ::bifrost::data::BIN2D);
    in_range_update = false;
  }
  // 1-D ranges are restored on the next timer-driven plot() call
}

// ── Per-cell accessors ────────────────────────────────────────────────────────

PlotManager::Dim PlotManager::dim(int i, int j) const {
  auto k = key(i, j);
  return dims.count(k) ? dims.at(k) : Dim::none;
}
PlotManager::type_t PlotManager::type_at(int i, int j) const {
  auto k = key(i, j);
  return types.count(k) ? types.at(k) : type_t::unknown;
}
bool PlotManager::is_flipped(int i, int j) const {
  auto k = key(i, j);
  return flipped.count(k) && flipped.at(k);
}
QCustomPlot * PlotManager::plot_at(int i, int j) const {
  auto k = key(i, j);
  return plots.count(k) ? plots.at(k) : nullptr;
}
QCPColorMap * PlotManager::image_at(int i, int j) const {
  auto k = key(i, j);
  return images.count(k) ? images.at(k) : nullptr;
}

// ── make_plot / make_1D / make_2D / set_axis_labels ──────────────────────────

void PlotManager::make_plot(int i, int j, bool flip, type_t t){
  auto item = layout->itemAtPosition(i, j);
  if (!item){
    auto p = new QCustomPlot();
    p->axisRect()->setAutoMargins(QCP::msNone);
    p->xAxis->setTicks(false);
    p->yAxis->setTicks(false);
    p->xAxis->setTickPen(QPen(Qt::NoPen));
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

void PlotManager::make_1D(int i, int j, bool flip, type_t t){
  using ::bifrost::data::Filter;
  auto k = key(i, j);
  dims[k] = Dim::one;
  in_range_update = true;   // block initial setup from marking this as user-zoomed
  make_plot(i, j, flip, t);

  plots[k]->yAxis->setTicks(true);
  plots[k]->xAxis->setTicks(true);
  plots[k]->yAxis->setTickLabels(true);
  plots[k]->xAxis->setTickLabels(true);
  plots[k]->axisRect()->setupFullAxesBox();
  in_range_update = false;

  std::vector<std::pair<Filter, QColor>> filter_color{
      {{Filter::none, Qt::black}, {Filter::positive, Qt::darkGreen}, {Filter::negative, Qt::darkRed}}
  };
  for (const auto & [filter, color]: filter_color){
    auto lk = key(i, j, filter);
    lines[lk] = new QCPGraph(flip ? plots[k]->yAxis : plots[k]->xAxis, flip ? plots[k]->xAxis : plots[k]->yAxis);
    lines[lk]->setLineStyle(QCPGraph::LineStyle::lsStepCenter);
    lines[lk]->setPen(QPen(color));
  }
  set_axis_labels(i, j);
}

void PlotManager::make_2D(int i, int j, bool flip, type_t t){
  using ::bifrost::data::Filter;
  dims[key(i, j)] = Dim::two;
  in_range_update = true;   // block initial setup from marking this as user-zoomed
  make_plot(i, j, flip, t);
  auto p = plots[key(i, j)];
  p->xAxis->setRange(0, n2);
  p->yAxis->setRange(0, n2);
  p->axisRect()->setupFullAxesBox();
  in_range_update = false;

  auto m = new QCPColorMap(flip ? p->yAxis : p->xAxis, flip ? p->xAxis : p->yAxis);
  m->data()->setSize(n2, n2);
  m->data()->setRange(QCPRange(0, n2-1), QCPRange(0, n2-1));
  m->setTightBoundary(false);
  m->setInterpolate(false);

  auto s = new QCPColorScale(p);
  m->setColorScale(s);
  m->setGradient(QCPColorGradient::gpGrayscale);
  m->rescaleDataRange();
  images[key(i, j)] = m;

  std::vector<std::pair<Filter, QColor>> filter_color{
      {{Filter::none, Qt::green}, {Filter::positive, Qt::yellow}, {Filter::negative, Qt::magenta}}
  };
  for (const auto & [filter, color]: filter_color){
    auto pk = key(i, j, filter);
    polygons[pk] = new QCPCurve(flip ? p->yAxis : p->xAxis, flip ? p->xAxis : p->yAxis);
    polygons[pk]->setLineStyle(QCPCurve::LineStyle::lsLine);
    polygons[pk]->setPen(QPen(color));
  }
  set_axis_labels(i, j);
}

void PlotManager::set_axis_labels(int i, int j) {
  auto k = key(i, j);
  if (!plots.count(k) || !types.count(k)) return;
  auto names = ::bifrost::data::axes_names(types.at(k));
  bool flip = flipped.count(k) && flipped.at(k);
  auto * p = plots.at(k);
  if (dims.at(k) == Dim::one) {
    QString var = names.empty() ? "" : QString::fromStdString(names[0]);
    (flip ? p->yAxis : p->xAxis)->setLabel(var);
    (flip ? p->xAxis : p->yAxis)->setLabel("Count");
  } else if (dims.at(k) == Dim::two && names.size() >= 2) {
    // flip: colormap key=yAxis (names[0]), value=xAxis (names[1])
    (flip ? p->yAxis : p->xAxis)->setLabel(QString::fromStdString(names[0]));
    (flip ? p->xAxis : p->yAxis)->setLabel(QString::fromStdString(names[1]));
  }
}

// ── named_colormap ────────────────────────────────────────────────────────────

QCPColorGradient named_colormap(std::string_view name, bool invert){
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
#pragma once
#include <map>
#include <QGridLayout>
#include <QPlot/qcustomplot/qcustomplot.h>
#include <fmt/format.h>
#include "data_manager.h"
#include <iostream>


class PlotManager{
public:
    //using data_t = ::bifrost::data::Manager;
    using type_t = ::bifrost::data::Type;
    using layout_t = QGridLayout;
    enum class Dim {none, one, two};

    PlotManager(layout_t * l, int n1, int n2): layout(l), n1(n1), n2(n2) {
        dims[0] = Dim::none;
    }

    void make_single(Dim d, type_t t){
        if (layout->rowCount() != 1 || layout->columnCount() != 1 || d != dims[0]){
          empty_layout();
          // why is this _always_ 3x3?
          // std::cout << layout->columnCount() << " " << layout->rowCount() << std::endl;
          if (Dim::one == d) return make_1D(0, 0, false, t);
          if (Dim::two == d) return make_2D(0, 0, false, t);
          // std::cout << layout->columnCount() << " " << layout->rowCount() << std::endl;
        }
    }

    void make_all_same(Dim d, type_t t){
        empty_layout();
        for (int i=0; i<3; ++i) {
            for (int j=0; j<3; ++j) {
                // if the plot exists but is the wrong type, remove it.
                if (layout->itemAtPosition(i, j) && d !=dims[key(i, j)]){
                  remove(i, j);
                }
                // if the plot doesn't exist create it
                if (!layout->itemAtPosition(i, j)) {
                    if (Dim::one == d){
                        make_1D(i, j, false, t);
                    } else {
                        make_2D(i, j, false, t);
                    }
                }
            }
        }
    }

    void make_multi(std::array<type_t, 9> ts){
        /*
         * 1 1 1
         * 2 2 -
         * 2 2 -
         */
        empty_layout();
        for (int i=0; i<3; ++i) {
            for (int j=0; j<3; ++j) {
                Dim t{i == 0 || j > 1 ? Dim::one : Dim::two};
                // if the plot exists but is the wrong type, remove it.
                if (layout->itemAtPosition(i, j) && t !=dims[key(i, j)]){
                  remove(i, j);
                }
                // if the plot doesn't exist create it
                if (!layout->itemAtPosition(i, j)) {
                    if (Dim::one == t){
                        make_1D(i, j, (i>0) & (j>1), ts[i*3 + j]);
                    } else {
                        make_2D(i, j, false, ts[i*3 + j]);
                    }
                }
            }
        }
    }

    void plot(int i, int j, const std::vector<double> & x, const std::vector<double> & y, double min, double max, bool is_log){
        QVector<double> q_x(x.begin(), x.end());
        QVector<double> q_y(y.begin(), y.end());
        plot(i, j, &q_x, &q_y, min, max, is_log);
    }

    void plot(int i, int j, const QVector<double> * x, const QVector<double> * y, double min, double max, bool is_log){
      // 1-D data plotting
      auto k = key(i, j);
      if (!dims.count(k) || dims.at(k) != Dim::one) return;
      auto g = lines.at(k);
      g->setData(*x, *y);
      auto p = plots.at(k);
      QCPAxis * independent{flipped[k] ? p->yAxis : p->xAxis};
      independent->setRange(x->front(), x->back());
      // apply scaling
      QCPAxis * ax{flipped[k] ? p->xAxis : p->yAxis};
      ax->setScaleType(is_log ? QCPAxis::stLogarithmic : QCPAxis::stLinear);
      ax->setRange(min - (max - min) / 40, max + (max - min) / 20);
    }

  void plot(int i, int j, QCPColorMapData * data, double min, double max, bool is_log, std::string_view gradient, bool is_inverted){
    auto k = key(i, j);
    if (!dims.count(k) || dims.at(k) != Dim::two) return;
    if (!images.count(k)) return;
    auto im = images.at(k);
    // Why does setData _require_ a mutable pointer?
    im->setData(data);
    auto p = plots.at(k);
    p->xAxis->setRange(0, ::bifrost::data::BIN2D);
    p->yAxis->setRange(0, ::bifrost::data::BIN2D);
    im->setGradient(named_colormap(gradient, is_inverted));
    im->setDataScaleType(is_log ? QCPAxis::stLogarithmic : QCPAxis::stLinear);
    im->setDataRange(QCPRange(min, max));
  }


private:
  [[nodiscard]] inline int key(int i, int j) const {
        return i * n_ + j;
    }

    void make_plot(int i, int j, bool flip, type_t t){
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
        }
    }
    void make_1D(int i, int j, bool flip, type_t t){
        dims[key(i, j)] = Dim::one;
        make_plot(i, j, flip, t);
        auto p = plots[key(i, j)];
        auto g = new QCPGraph(flip ? p->yAxis : p->xAxis, flip ? p->xAxis : p->yAxis);
        // styling of the displayed line
        lines[key(i, j)] = g;

        p->yAxis->setTicks(true);
        p->xAxis->setTicks(true);
        p->yAxis->setTickLabels(true);
        p->xAxis->setTickLabels(true);
        p->axisRect()->setupFullAxesBox();

        g->setLineStyle(QCPGraph::LineStyle::lsStepCenter);

        //p->setInteractions(QCP::iRangeDrag| QCP::iRangeZoom | QCP::iSelectPlottables);
    }
    void make_2D(int i, int j, bool flip, type_t t){
      dims[key(i, j)] = Dim::two;
      make_plot(i, j, flip, t);
      auto p = plots[key(i, j)];
      p->xAxis->setRange(0, n2);
      p->yAxis->setRange(0, n2);
      p->axisRect()->setupFullAxesBox();

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
    }


private:
    //data_t * histograms;
    layout_t * layout{};
    int n1;
    int n2;
    int n_{3};

    std::map<int, QCustomPlot *> plots;
    std::map<int, QCPColorMap *> images;
    std::map<int, QCPGraph *> lines;
    std::map<int, Dim> dims;
    std::map<int, type_t> types;
    std::map<int, bool> flipped;

    void clear(){
        qDeleteAll(layout->children());
        plots.clear();
        images.clear();
        lines.clear();
        dims.clear();
        types.clear();
        flipped.clear();
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
    }

    void empty_layout(){
      for (int i=0; i<layout->rowCount(); ++i){
        for (int j=0; j<layout->columnCount(); ++j){
          remove(i, j);
        }
      }
    }

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
};
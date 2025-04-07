// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief GUI fylgje application interface to handle binning data from AR51 format messages
/// \note This wrapper is need since it provides QCustomPlot-encapsulated data for plotting
//===----------------------------------------------------------------------===//
#include "QHistogramManager.h"

using namespace bifrost::data::Q;


double HistogramManager::max(Filter which) const {
  double x{std::numeric_limits<double>::lowest()};
  for (int a=0; a<arcs; ++a) if (auto y = max(a, which); y > x) x = y;
  return x;
}

double HistogramManager::max(int arc, Filter which) const {
  double x{std::numeric_limits<double>::lowest()};
  for (int t=0; t<triplets; ++t) if (auto y = max(arc, t, which); y > x) x = y;
  return x;
}

double HistogramManager::max(int arc, int triplet, Filter which) const {
  double x{std::numeric_limits<double>::lowest()};
  if (arc < 0 || arc >= arcs || triplet < 0 || triplet >= triplets) return x;
  for (auto t: TYPEND) if (auto y = max(arc, triplet, t, which); y > x) x = y;
  return x;
}

double HistogramManager::max(int arc, Type type, Filter which) const {
  double x{std::numeric_limits<double>::lowest()};
  for (int t=0; t<triplets; ++t)if (auto y = max(arc, t, type, which); y > x) x = y;
  return x;
}

double HistogramManager::max(int arc, int triplet, bifrost::data::Type t, Filter which) const {
  return max(key(arc, triplet, t), which);
}

double HistogramManager::max(bifrost::data::key_t k, Filter which) const {
  return is_1D(key_type(k)) ? max_1D(k, which) : max_2D(k, which);
}

double HistogramManager::min(Filter which) const {
  double x{(std::numeric_limits<double>::max)()};
  for (int a=0; a<arcs; ++a) if (auto y = min(a, which);y < x) x = y;
  return x;
}

double HistogramManager::min(int arc, Filter which) const {
  double x{(std::numeric_limits<double>::max)()};
  for (int t=0; t<triplets; ++t) if (auto y = min(arc, t, which); y < x) x = y;
  return x;
}

double HistogramManager::min(int arc, int triplet, Filter which) const {
  double x{(std::numeric_limits<double>::max)()};
  if (arc < 0 || arc >= arcs || triplet < 0 || triplet >= triplets) return x;
  for (auto t: TYPEND) if (auto y = min(arc, triplet, t, which); y < x) x = y;
  return x;
}

double HistogramManager::min(int arc, Type type, Filter which) const {
  double x{(std::numeric_limits<double>::max)()};
  for (int t=0; t<triplets; ++t) if (auto y = min(arc, t, type, which); y < x) x = y;
  return x;
}

double HistogramManager::min(int arc, int triplet, bifrost::data::Type t, Filter which) const {
  return min(key(arc, triplet, t), which);
}

double HistogramManager::min(bifrost::data::key_t k, Filter which) const {
  return is_1D(key_type(k)) ? min_1D(k, which) : min_2D(k, which);
}


HistogramManager::D1 HistogramManager::data_1D(int arc, int triplet, bifrost::data::Type t, Filter which) const {
  return data_1D(this->key(arc, triplet, t), which);
}

HistogramManager::D1 HistogramManager::data_1D(bifrost::data::key_t k, Filter which) const {
  auto d = HistogramManager::D1();
  auto this_type = key_type(k);
  const auto & data{which == Filter::none ? everything : which == Filter::positive ? included : excluded};
  if (data.count(k) && bins_1d.count(this_type)){
    auto full = data.at(k);
    auto bins = bins_1d.at(this_type);
    d.reserve(bins);
    for (int i=0; i<bins; ++i) d.push_back(0.);
    if (BIN1D == bins){
      for (int i=0; i<BIN1D; ++i){
        d[i] = full.at(i);
      }
    } else {
      auto r = BIN1D / bins; // since all bins are powers of two, this is as well
      for (int i=0; i < bins; ++i){
        for (int j=0; j < r; ++j){
          d[i] += full.at(i*r + j);
        }
        d[i] /= r;
      }
    }
  }
  return d;
}

HistogramManager::D2 * HistogramManager::data_2D(int arc, int triplet, bifrost::data::Type t, Filter which) const {
  return data_2D(key(arc, triplet, t), which);
}

HistogramManager::D2 * HistogramManager::data_2D(bifrost::data::key_t k, Filter which) const {
  // translate 2d to 1d axes
  auto this_type = key_type(k);
  auto [nx, ny] = bins_2D(this_type);
  int bx{BIN2D/nx/2}, by{BIN2D/ny/2};
  double norm{static_cast<double>(BIN2D)*static_cast<double>(BIN2D)/static_cast<double>(nx)/static_cast<double>(ny)};

  auto d = new D2(nx, ny, QCPRange(bx, BIN2D-bx), QCPRange(by, BIN2D-by));
  d->fill(0);
  const auto & data{which == Filter::none ? everything : which == Filter::positive ? included : excluded};
  if (data.count(k)){
    auto full = data.at(k);
    if (nx == BIN2D && ny == BIN2D){
      for (int ix=0; ix < nx; ++ix){
        for (int iy=0; iy < ny; ++iy){
          d->setCell(ix, iy, full.at(ix * BIN2D + iy));
        }
      }
    } else {
      auto rx = BIN2D / nx;
      auto ry = BIN2D / ny;
      for (int ix=0; ix < nx; ++ix){
        for (int iy=0; iy < ny; ++iy){
          double tmp{0.};
          for (int jx=0; jx < rx; ++jx){
            for (int jy=0; jy < ry; ++jy){
              auto z = (ix * rx + jx) * BIN2D + (iy * ry + jy);
              tmp += full.at(z);
            }
          }
          d->setCell(ix, iy, tmp/norm);
        }
      }
    }
  }
  return d;
}


int HistogramManager::max_1D(bifrost::data::key_t k, Filter which) const {
  int value{std::numeric_limits<int>::lowest()};
  auto this_type = key_type(k);
  const auto & data{which == Filter::none ? everything : which == Filter::positive ? included : excluded};
  if (data.count(k) && bins_1d.count(this_type)){
    auto full = data.at(k);
    auto bins = bins_1d.at(this_type);
    if (BIN1D == bins){
      for (int i=0; i<BIN1D; ++i){
        if (full.at(i) > value) value = full.at(i);
      }
    } else {
      auto r = BIN1D / bins; // since all bins are powers of two, this is as well
      for (int i=0; i < bins; ++i){
        int tmp{0};
        for (int j=0; j < r; ++j){
          tmp += full.at(i*r + j);
        }
        tmp = tmp ? tmp/r ? tmp/r : 1 : 0;
        if (tmp > value) value = tmp;
      }
    }
  }
  return value;
}

int HistogramManager::max_2D(bifrost::data::key_t k, Filter which) const {
  auto [nx, ny] = bins_2D(key_type(k));
  int value{std::numeric_limits<int>::lowest()};
  double norm{static_cast<double>(BIN2D)*static_cast<double>(BIN2D)/static_cast<double>(nx)/static_cast<double>(ny)};
  const auto & data{which == Filter::none ? everything : which == Filter::positive ? included : excluded};
  if (data.count(k)){
    auto & full = data.at(k);
    if (nx == BIN2D && ny == BIN2D){
      for (int ix=0; ix < nx; ++ix){
        for (int iy=0; iy < ny; ++iy){
          if (full.at(ix * BIN2D + iy) > value) value = full.at(ix * BIN2D + iy);
        }
      }
    } else {
      auto rx = BIN2D / nx;
      auto ry = BIN2D / ny;
      for (int ix=0; ix < nx; ++ix){
        for (int iy=0; iy < ny; ++iy){
          int tmp{0};
          for (int jx=0; jx < rx; ++jx){
            for (int jy=0; jy < ry; ++jy){
              auto z = (ix * rx + jx) * BIN2D + (iy * ry + jy);
              tmp += full.at(z);
            }
          }
          if (tmp > value) value = tmp;
        }
      }
    }
  }
  return static_cast<int>(std::ceil(static_cast<double>(value)/norm));
}


int HistogramManager::min_1D(bifrost::data::key_t k, Filter which) const {
  auto this_type = key_type(k);
  int value{(std::numeric_limits<int>::max)()};
  const auto & data{which == Filter::none ? everything : which == Filter::positive ? included : excluded};
  if (data.count(k) && bins_1d.count(this_type)){
    auto full = data.at(k);
    auto bins = bins_1d.at(this_type);
    if (BIN1D == bins){
      for (int i=0; i<BIN1D; ++i){
        if (full.at(i) < value) value = full.at(i);
      }
    } else {
      auto r = BIN1D / bins; // since all bins are powers of two, this is as well
      for (int i=0; i < bins; ++i){
        int tmp{0};
        for (int j=0; j < r; ++j){
          tmp += full.at(i*r + j);
        }
        tmp = tmp ? tmp/r ? tmp/r : 1 : 0;
        if (tmp < value) value = tmp;
      }
    }
  }
  return value;
}

int HistogramManager::min_2D(bifrost::data::key_t k, Filter which) const {
  auto this_type = key_type(k);
  auto [nx, ny] = bins_2D(this_type);
  double norm{static_cast<double>(BIN2D)*static_cast<double>(BIN2D)/static_cast<double>(nx)/static_cast<double>(ny)};
  int value{(std::numeric_limits<int>::max)()};
  const auto & data{which == Filter::none ? everything : which == Filter::positive ? included : excluded};
  if (data.count(k)){
    auto full = data.at(k);
    if (nx == BIN2D && ny == BIN2D){
      for (int ix=0; ix < nx; ++ix){
        for (int iy=0; iy < ny; ++iy){
          if (full.at(ix * BIN2D + iy) < value) value = full.at(ix * BIN2D + iy);
        }
      }
    } else {
      auto rx = BIN2D / nx;
      auto ry = BIN2D / ny;
      for (int ix=0; ix < nx; ++ix){
        for (int iy=0; iy < ny; ++iy){
          int tmp{0};
          for (int jx=0; jx < rx; ++jx){
            for (int jy=0; jy < ry; ++jy){
              auto z = (ix * rx + jx) * BIN2D + (iy * ry + jy);
              tmp += full.at(z);
            }
          }
          if (tmp < value) value = tmp;
        }
      }
    }
  }
  return static_cast<int>(std::ceil(static_cast<double>(value)/norm));
}

std::pair<int, int> HistogramManager::bins_2D(bifrost::data::Type t) const {
  Type x{Type::unknown}, y;
  if (Type::ab == t){
    x = Type::a;
    y = Type::b;
  } else if (Type::pt == t){
    x = Type::p;
    y = Type::t;
  } else if (Type::xt == t){
    x = Type::x;
    y = Type::t;
  } else if (Type::xp == t){
    x = Type::x;
    y = Type::p;
  }
  if (x == Type::unknown) return std::make_pair(-1, -1);
  if (!bins_2d.count(x) || !bins_2d.count(y)) return std::make_pair(0, 0);
  return std::make_pair(bins_2d.at(x), bins_2d.at(y));
}
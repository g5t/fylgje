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


double HistogramManager::max(const Filter which) const {
  double x{std::numeric_limits<double>::lowest()};
  for (int a=0; a<arcs; ++a) if (const auto y = max(a, which); y > x) x = y;
  return x;
}

double HistogramManager::max(const int arc, const Filter which) const {
  double x{std::numeric_limits<double>::lowest()};
  for (int t=0; t<triplets; ++t) if (const auto y = max(arc, t, which); y > x) x = y;
  return x;
}

double HistogramManager::max(const int arc, const int triplet, const Filter which) const {
  double x{std::numeric_limits<double>::lowest()};
  if (arc < 0 || arc >= arcs || triplet < 0 || triplet >= triplets) return x;
  for (const auto t: TYPEND) if (const auto y = max(arc, triplet, t, which); y > x) x = y;
  return x;
}

double HistogramManager::max(const int arc, const Type type, const Filter which) const {
  double x{std::numeric_limits<double>::lowest()};
  for (int t=0; t<triplets; ++t) if (const auto y = max(arc, t, type, which); y > x) x = y;
  return x;
}

double HistogramManager::max(const int arc, const int triplet, const Type type, const Filter which) const {
  return max(key(arc, triplet, type), which);
}

double HistogramManager::max(const key_t key, const Filter which) const {
  return is_1D(key_type(key)) ? max_1D(key, which) : max_2D(key, which);
}

double HistogramManager::min(const Filter which) const {
  double x{(std::numeric_limits<double>::max)()};
  for (int a=0; a<arcs; ++a) if (const auto y = min(a, which); y < x) x = y;
  return x;
}

double HistogramManager::min(const int arc, const Filter which) const {
  double x{(std::numeric_limits<double>::max)()};
  for (int t=0; t<triplets; ++t) if (const auto y = min(arc, t, which); y < x) x = y;
  return x;
}

double HistogramManager::min(const int arc, const int triplet, const Filter which) const {
  double x{(std::numeric_limits<double>::max)()};
  if (arc < 0 || arc >= arcs || triplet < 0 || triplet >= triplets) return x;
  for (const auto t: TYPEND) if (const auto y = min(arc, triplet, t, which); y < x) x = y;
  return x;
}

double HistogramManager::min(const int arc, const Type type, const Filter which) const {
  double x{(std::numeric_limits<double>::max)()};
  for (int t=0; t<triplets; ++t) if (const auto y = min(arc, t, type, which); y < x) x = y;
  return x;
}

double HistogramManager::min(const int arc, const int triplet, const Type type, const Filter which) const {
  return min(key(arc, triplet, type), which);
}

double HistogramManager::min(const key_t key, const Filter which) const {
  return is_1D(key_type(key)) ? min_1D(key, which) : min_2D(key, which);
}


HistogramManager::D1 HistogramManager::data_1D(const int arc, const int triplet, const Type type, const Filter which) const {
  return data_1D(this->key(arc, triplet, type), which);
}

HistogramManager::D1 HistogramManager::data_1D(const key_t key, const Filter which) const {
  auto d = HistogramManager::D1();
  const auto this_type = key_type(key);
  if (const auto & data{which == Filter::none ? everything : which == Filter::positive ? included : excluded}; data.count(key) && bins_1d.count(this_type)){
    const auto full = data.at(key);
    const auto bins = bins_1d.at(this_type);
    d.reserve(bins);
    for (int i=0; i<bins; ++i) d.push_back(0.);
    if (BIN1D == bins){
      for (int i=0; i<BIN1D; ++i){
        d[i] = full.at(i);
      }
    } else {
      const auto r = BIN1D / bins; // since all bins are powers of two, this is as well
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

HistogramManager::D2 * HistogramManager::data_2D(const int arc, const int triplet, const Type type, const Filter which) const {
  return data_2D(key(arc, triplet, type), which);
}

HistogramManager::D2 * HistogramManager::data_2D(const key_t key, const Filter which) const {
  // translate 2d to 1d axes
  const auto this_type = key_type(key);
  const auto [nx, ny] = bins_2D(this_type);
  const int bx{BIN2D/nx/2};
  const int by{BIN2D/ny/2};
  const double norm{static_cast<double>(BIN2D)*static_cast<double>(BIN2D)/static_cast<double>(nx)/static_cast<double>(ny)};

  const auto d = new D2(nx, ny, QCPRange(bx, BIN2D-bx), QCPRange(by, BIN2D-by));
  d->fill(0);
  if (const auto & data{which == Filter::none ? everything : which == Filter::positive ? included : excluded}; data.count(key)){
    const auto full = data.at(key);
    if (nx == BIN2D && ny == BIN2D){
      for (int ix=0; ix < nx; ++ix){
        for (int iy=0; iy < ny; ++iy){
          d->setCell(ix, iy, full.at(ix * BIN2D + iy));
        }
      }
    } else {
      const auto rx = BIN2D / nx;
      const auto ry = BIN2D / ny;
      for (int ix=0; ix < nx; ++ix){
        for (int iy=0; iy < ny; ++iy){
          double tmp{0.};
          for (int jx=0; jx < rx; ++jx){
            for (int jy=0; jy < ry; ++jy){
              const auto z = (ix * rx + jx) * BIN2D + (iy * ry + jy);
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


int HistogramManager::max_1D(const key_t t, const Filter which) const {
  int value{std::numeric_limits<int>::lowest()};
  const auto this_type = key_type(t);
  if (const auto & data{which == Filter::none ? everything : which == Filter::positive ? included : excluded}; data.count(t) && bins_1d.count(this_type)){
    const auto full = data.at(t);
    if (const auto bins = bins_1d.at(this_type); BIN1D == bins){
      for (int i=0; i<BIN1D; ++i){
        if (full.at(i) > value) value = full.at(i);
      }
    } else {
      const auto r = BIN1D / bins; // since all bins are powers of two, this is as well
      for (int i=0; i < bins; ++i){
        int tmp{0};
        for (int j=0; j < r; ++j){
          tmp += full.at(i*r + j);
        }
        if (tmp) {
          const auto tmp_over_r = tmp/r;
          tmp = tmp_over_r ? tmp_over_r : 1;
        }
        if (tmp > value) value = tmp;
      }
    }
  }
  return value;
}

int HistogramManager::max_2D(const key_t t, const Filter which) const {
  const auto [nx, ny] = bins_2D(key_type(t));
  int value{std::numeric_limits<int>::lowest()};
  const double norm{static_cast<double>(BIN2D)*static_cast<double>(BIN2D)/static_cast<double>(nx)/static_cast<double>(ny)};
  if (const auto & data{which == Filter::none ? everything : which == Filter::positive ? included : excluded}; data.count(t)){
    auto & full = data.at(t);
    if (nx == BIN2D && ny == BIN2D){
      for (int ix=0; ix < nx; ++ix){
        for (int iy=0; iy < ny; ++iy){
          if (full.at(ix * BIN2D + iy) > value) value = full.at(ix * BIN2D + iy);
        }
      }
    } else {
      const auto rx = BIN2D / nx;
      const auto ry = BIN2D / ny;
      for (int ix=0; ix < nx; ++ix){
        for (int iy=0; iy < ny; ++iy){
          int tmp{0};
          for (int jx=0; jx < rx; ++jx){
            for (int jy=0; jy < ry; ++jy){
              const auto z = (ix * rx + jx) * BIN2D + (iy * ry + jy);
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


int HistogramManager::min_1D(const key_t t, const Filter which) const {
  const auto this_type = key_type(t);
  int value{(std::numeric_limits<int>::max)()};
  if (const auto & data{which == Filter::none ? everything : which == Filter::positive ? included : excluded}; data.count(t) && bins_1d.count(this_type)){
    const auto full = data.at(t);
    if (const auto bins = bins_1d.at(this_type); BIN1D == bins){
      for (int i=0; i<BIN1D; ++i){
        if (full.at(i) < value) value = full.at(i);
      }
    } else {
      const auto r = BIN1D / bins; // since all bins are powers of two, this is as well
      for (int i=0; i < bins; ++i){
        int tmp{0};
        for (int j=0; j < r; ++j){
          tmp += full.at(i*r + j);
        }
        if (tmp) {
          const auto tmp_over_r = tmp/r;
          tmp = tmp_over_r ? tmp_over_r : 1;
        }
        if (tmp < value) value = tmp;
      }
    }
  }
  return value;
}

int HistogramManager::min_2D(const key_t t, const Filter which) const {
  const auto this_type = key_type(t);
  const auto [nx, ny] = bins_2D(this_type);
  const double norm{static_cast<double>(BIN2D)*static_cast<double>(BIN2D)/static_cast<double>(nx)/static_cast<double>(ny)};
  int value{(std::numeric_limits<int>::max)()};
  if (const auto & data{which == Filter::none ? everything : which == Filter::positive ? included : excluded}; data.count(t)){
    const auto full = data.at(t);
    if (nx == BIN2D && ny == BIN2D){
      for (int ix=0; ix < nx; ++ix){
        for (int iy=0; iy < ny; ++iy){
          if (full.at(ix * BIN2D + iy) < value) value = full.at(ix * BIN2D + iy);
        }
      }
    } else {
      const auto rx = BIN2D / nx;
      const auto ry = BIN2D / ny;
      for (int ix=0; ix < nx; ++ix){
        for (int iy=0; iy < ny; ++iy){
          int tmp{0};
          for (int jx=0; jx < rx; ++jx){
            for (int jy=0; jy < ry; ++jy){
              const auto z = (ix * rx + jx) * BIN2D + (iy * ry + jy);
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

std::pair<int, int> HistogramManager::bins_2D(const Type t) const {
  if (Type::ab != t && Type::pt != t && Type::xt != t && Type::xp != t) {
    return std::make_pair(0, 0);
  }
  Type x, y;
  if (Type::ab == t){
    x = Type::a;
    y = Type::b;
  } else if (Type::pt == t){
    x = Type::p;
    y = Type::t;
  } else if (Type::xt == t){
    x = Type::x;
    y = Type::t;
  } else {
    x = Type::x;
    y = Type::p;
  }
  if (!bins_2d.count(x) || !bins_2d.count(y)) return std::make_pair(0, 0);
  return std::make_pair(bins_2d.at(x), bins_2d.at(y));
}
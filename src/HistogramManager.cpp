// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief fylgje interface to handle binning events from AR51 format messages
//===----------------------------------------------------------------------===//
#include <iostream>
#include <fmt/format.h>
#include <sstream>

#include "HistogramManager.h"

int bifrost::data::hist_a_or_b(int x, int shift, int bins){
  int y = x >> shift;
  if (y < 0 || y >= bins) y = -1;
  return y;
}

int bifrost::data::hist_p(int x, int shift, int bins){
  // a and b are effectively 15-bit integers
  // so a+b is 16-bits, but we want this to fit into BIN2D bins,
  // so we must shift by an extra bit compared to a or b above
  int y = x >> (shift + 1);
  if (y < 0 || y >= bins) y = -1;
  return y;
}

int bifrost::data::hist_t(double x, int bins){
  // time at ESS resets every 1/14 Hz ~= 70 msec.
  // find the modulus and bin that range
  double period = 1.0 / 14.0;
  auto frac = fmod(x, period) / period;
  auto y = static_cast<int>(frac * bins);
  if (y < 0 || y >= bins) y = -1;
  return y;
}

int bifrost::data::hist_x(int a, int b, int bins){
  int num = a - b;
  int den = a + b;
  if (den == 0) {
    return false;
  }
  double ratio = static_cast<double>(num) / static_cast<double>(den);
  if (ratio < -1.0 || ratio > 1.0) {
    return false;
  }
  // full range is (-1, 1) so shift up by 1, multiply by 512 and convert to an integer
  auto x = static_cast<int>((ratio + 1.0) / 2.0 * (bins - 1));
  if (x < 0 || x >= bins) x = -1;
  return x;
}


bool bifrost::data::HistogramManager::add(const bifrost::message_t & message, bool allowed) {
  auto arc_ = arc(message.group);
  auto triplet_ = triplet(message.fiber, message.group);
  if (arc_ < 0 || arc_ >= arcs || triplet_ < 0 || triplet_ >= triplets) {
    return false;
  }

  bool ok{true};
  ok &= add_1D(arc_, triplet_, message.a, message.b, message.time, allowed);
  ok &= add_2D(arc_, triplet_, message.a, message.b, message.time, allowed);
  return ok;
}


bool bifrost::data::HistogramManager::add_1D(int arc, int triplet, int a, int b, double time, bool allowed){
  auto t_a = std::make_pair(Type::a, hist_a_or_b(a, SHIFT1D, BIN1D));
  auto t_b = std::make_pair(Type::b, hist_a_or_b(b, SHIFT1D, BIN1D));
  auto t_p = std::make_pair(Type::p, hist_p(a+b, SHIFT1D, BIN1D));
  auto t_x = std::make_pair(Type::x, hist_x(a, b, BIN1D));
  auto t_t = std::make_pair(Type::t, hist_t(time, BIN1D));
  if (t_a.second < 0 || t_b.second < 0 || t_p.second < 0 || t_x.second < 0 || t_t.second < 0) {
    return false;
  }
  std::vector<std::pair<map_t<data_t> *, bool>> each{{{&everything, true}, {&included, allowed}, {&excluded, !allowed}}};
  for (auto & [data, tf]: each) {
    if (tf) {
      for (auto [t, h]: {t_a, t_b, t_p, t_x, t_t}) {
        auto this_key = this->key(arc, triplet, t);
        if (!data->count(this_key)) {
          std::stringstream ss;
          ss << t;
          fmt::print("arc={}, triplet={}, type={} does not exist in managed data\n", arc, triplet, ss.str());
          return false;
        }
        if (static_cast<size_t>(h) < data->at(this_key).size()) {
          data->at(this_key).at(h) += 1;
        } else {
          std::stringstream ss;
          ss << t;
          fmt::print("arc={}, triplet={}, type={} at a={}, b={} time={} given out of bound index {}\n", arc, triplet,
                     ss.str(), a, b, time, h);
        }
      }
    }
  }
  return true;
}


bool bifrost::data::HistogramManager::add_2D(int arc, int triplet, int full_a, int full_b, double full_t, bool allowed){
  auto a = hist_a_or_b(full_a, SHIFT2D, BIN2D);
  auto b = hist_a_or_b(full_b, SHIFT2D, BIN2D);
  auto p = hist_p(full_a+full_b, SHIFT2D, BIN2D);
  auto x = hist_x(full_a, full_b, BIN2D);
  auto t = hist_t(full_t, BIN2D);
  if (a < 0 || b < 0 || p < 0 || x < 0 || t < 0) return false;
  std::vector<std::tuple<Type, int, int>> t_i_j {{Type::ab, a, b}, {Type::pt, p, t}, {Type::xt, x, t}, {Type::xp, x, p}};
  std::vector<std::pair<map_t<data_t> *, bool>> each{{{&everything, true}, {&included, allowed}, {&excluded, !allowed}}};
  for (auto & [data, tf]: each) {
    if (tf) {
      for (auto [y, i, j]: t_i_j) {
        auto this_key = this->key(arc, triplet, y);
        if (!data->count(this_key)) {
          std::stringstream ss;
          ss << y;
          fmt::print("arc={}, triplet={}, type={} does not exist in managed data\n", arc, triplet, ss.str());
          return false;
        }
        auto ij = i * BIN2D + j;
        if (static_cast<size_t>(ij) < data->at(this_key).size()) {
          data->at(this_key).at(ij) += 1;
        } else {
          std::stringstream ss;
	  ss << "arc=" << arc << ", triplet=" << triplet << ", type=" << y;
	  ss << " at a=" << full_a << ", b=" << full_b << ", time=" << t;
	  ss << " given out of bounds index (" << i << ", " << j << ")\n)";
	  fmt::print(ss.str());
        }
      }
    }
  }
  return true;
}


bool bifrost::data::is_1D(bifrost::data::Type t) {
  using bifrost::data::TYPE1D;
  return std::find(std::begin(TYPE1D), std::end(TYPE1D), t) != std::end(TYPE1D);
}


bool bifrost::data::is_2D(bifrost::data::Type t) {
  using bifrost::data::TYPE2D;
  return std::find(std::begin(TYPE2D), std::end(TYPE2D), t) != std::end(TYPE2D);
}


std::ostream & operator<<(std::ostream & os, ::bifrost::data::Type type){
  using ::bifrost::data::Type;
  switch (type){
    case Type::a: {os << "I(A)";  break;}
    case Type::x: {os << "I(x)";  break;}
    case Type::p: {os << "I(p)";  break;}
    case Type::xp: {os << "I(x,p)";  break;}
    case Type::ab: {os << "I(A,B)";  break;}
    case Type::b: {os << "I(B)";  break;}
    case Type::xt: {os << "I(x,t)";  break;}
    case Type::pt: {os << "I(p,t)";  break;}
    case Type::t: {os << "I(t)";  break;}
    default: os << "unknown Type";
  }
  return os;
}


std::vector<std::string> bifrost::data::axes_names(Type type){
  switch (type){
    case Type::a: {return {"A"};}
    case Type::x: {return {"x"};}
    case Type::p: {return {"p"};}
    case Type::xp: {return {"x", "p"};}
    case Type::ab: {return {"A", "B"};}
    case Type::b: {return {"B"};}
    case Type::xt: {return {"x", "t"};}
    case Type::pt: {return {"p", "t"};}
    case Type::t: {return {"t"};}
    default: return {};
  }
}


std::string bifrost::data::type_dataset_name(Type type){
  switch (type){
    case Type::a: {return "A";}
    case Type::x: {return "x";}
    case Type::p: {return "p";}
    case Type::xp: {return "x_p";}
    case Type::ab: {return "A_B";}
    case Type::b: {return "B";}
    case Type::xt: {return "x_t";}
    case Type::pt: {return "p_t";}
    case Type::t: {return "t";}
    default: return {};
  }
}


std::vector<uint64_t> bifrost::data::HistogramManager::type_dimensions(Type type) const{
  switch (type){
    case Type::a: {return {BIN1D};}
    case Type::x: {return {BIN1D};}
    case Type::p: {return {BIN1D};}
    case Type::xp: {return {BIN2D, BIN2D};}
    case Type::ab: {return {BIN2D, BIN2D};}
    case Type::b: {return {BIN1D};}
    case Type::xt: {return {BIN2D, BIN2D};}
    case Type::pt: {return {BIN2D, BIN2D};}
    case Type::t: {return {BIN1D};}
    default: return {};
  }
}


void bifrost::data::HistogramManager::create_in(const hdf5::node::Group & parent) const {
  std::string creator{"fylgje"};
  std::string version{"v0.0.1"};
  std::string instrument{"BIFROST"};

  // create a group for the data
  auto group = parent.create_group("histograms");

  group.attributes.create_from("creator", creator);
  group.attributes.create_from("version", version);
  group.attributes.create_from("instrument", instrument);
  group.attributes.create_from("arcs", arcs);
  group.attributes.create_from("triplets", triplets);

  std::vector<std::string> data_order{{"arc"}, {"triplets"}, {"type"}};
  group.attributes.create_from("data_order", data_order);

  // all datasets are integer valued
  auto datatype = hdf5::datatype::create<int>();
  // their sizes never change, so use contiguous layout (rewritten in place)
  hdf5::property::DatasetCreationList datasetCreationList;
  datasetCreationList.layout(hdf5::property::DatasetLayout::Contiguous);

  // bin center values for 1-D and 2-D axes:
  // TODO add units for each axis
  auto axg = group.create_group("axes");
  auto ax1 = axg.create_group("1d");
  auto ax2 = axg.create_group("2d");
  auto dtf = hdf5::datatype::create<double>();
  for (auto & t: TYPE1D){
    auto dn = type_dataset_name(t);
    auto ds1 = hdf5::dataspace::Simple({BIN1D+1});
    auto ds2 = hdf5::dataspace::Simple({BIN2D+1});
    auto d1 = ax1.create_dataset(dn, dtf, ds1, datasetCreationList);
    auto d2 = ax2.create_dataset(dn, dtf, ds2, datasetCreationList);
    d1.attributes.create_from("axes", axes_names(t));
    d2.attributes.create_from("axes", axes_names(t));
    // the axis values are fixed, so write them at creation
    d1.write(axis(t, BIN1D+1));
    d2.write(axis(t, BIN2D+1));
  }
  std::string intensity_unit{"counts"};
  for (auto & name: {"everything", "included", "excluded"}){
    auto dg = group.create_group(name);
    for (int a = 0; a < arcs; ++a){
      auto arc_name = fmt::format("arc{}", a);
      auto da = dg.create_group(arc_name);
      for (int t = 0; t < triplets; ++t){
        auto triplet_name = fmt::format("triplet{}", t);
        auto dt = da.create_group(triplet_name);
        for (auto k: TYPEND){
          auto dataset_name = type_dataset_name(k);
          auto dsg = dt.create_group(dataset_name);
          auto dataspace = hdf5::dataspace::Simple(type_dimensions(k));
          auto ds = dsg.create_dataset("signal", datatype, dataspace, datasetCreationList);
          auto the_axes = axes_names(k);
          auto nax = the_axes.size();
          for (auto & ax: the_axes){
            auto p = hdf5::Path(fmt::format("./{}", ax));
            dsg.create_link(p, (nax == 1 ? ax1 : ax2).get_dataset(p));
          }
          ds.attributes.create_from("axes", the_axes);
          ds.attributes.create_from("unit", intensity_unit);
        }
      }
    }
  }
}

void bifrost::data::HistogramManager::write_to(const hdf5::node::Group & parent) const {
  auto group = parent.get_group("histograms");
  std::vector<std::pair<std::string, const map_t<data_t>*>> pairs{
      {{"everything", &everything}, {"included", &included}, {"excluded", &excluded}}
  };
  for (auto & [name, data]: pairs){
    auto dg = group.get_group(name);
    for (int a = 0; a < arcs; ++a){
      auto da = dg.get_group(fmt::format("arc{}", a));
      for (int t = 0; t < triplets; ++t){
        auto dt = da.get_group(fmt::format("triplet{}", t));
        for (auto k: TYPEND){
          auto ds = dt.get_group(type_dataset_name(k)).get_dataset("signal");
          ds.write(data->at(key(a, t, k)));
        }
      }
    }
  }
}

void bifrost::data::HistogramManager::save_to(const hdf5::node::Group & parent) const {
  create_in(parent);
  write_to(parent);
}


void bifrost::data::HistogramManager::save_to(const hdf5::file::File & file, const std::optional<std::string> & group) const {
  auto root = file.root();
  std::string name = group.value_or("fylgje");
  if (root.has_group(name)){
    throw std::runtime_error(fmt::format("The provided file already has the group /{}", name));
  }
  auto gr = root.create_group(name);
  save_to(gr);
}


void bifrost::data::HistogramManager::save_to(const std::filesystem::path & file, const std::optional<std::string> & group) const{
  namespace fs = std::filesystem;
  auto status = fs::status(file);
  hdf5::file::File hdf5_file;
  if (status.type() == fs::file_type::regular || status.type() == fs::file_type::symlink){
    // then it should be an HDF5 file that we can write to:
    if (!hdf5::file::is_hdf5_file(std::string(file))) {
      throw std::runtime_error(fmt::format("{} is not an HDF5 file", std::string(file)));
    }
    hdf5_file = hdf5::file::open(std::string(file), hdf5::file::AccessFlags::ReadWrite);
  } else if (status.type() == fs::file_type::not_found){
    hdf5_file = hdf5::file::create(std::string(file));
  } else {
    throw std::runtime_error(fmt::format("{} exists but is not a file", std::string(file)));
  }
  save_to(hdf5_file, group);
}

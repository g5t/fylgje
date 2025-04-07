// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief fylgje interface to assign pixel IDs like the EFU
//===----------------------------------------------------------------------===//
#include "PixelManager.h"

///\brief Replicate the EFU calculations to identify a unique pixel number
///\returns 0 if no valid pixel
int bifrost::data::PixelManager::pixel(int arc, int triplet, int a, int b) const {
  auto g = group(arc, triplet);
  auto pos = static_cast<double>(a) / static_cast<double>(a + b);
  auto tube = calibration.getUnitId(g, pos);
  if (tube < 0) {
    // invalid global position (outside any unit's range)
    return 0;
  }
  auto cor_pos = calibration.posCorrection(g, tube, calibration.unitPosition(g, tube,  pos)); // in range (0, 1)
  // corrected position in (0.0, 1.0) is mapped to a tube pixel in (0, pixels_per_tube - 1)
  // its offset by which tube it is, which triplet its in, and which arc its in
  int offset = pixels_per_tube * arc + pixels_per_tube * triplet + pixels_per_tube_arc * tube;
  // and note that valid pixels index from 1 -- not 0.
  return 1 + offset + static_cast<int>((pixels_per_tube - 1) * cor_pos);
}


bool bifrost::data::PixelManager::add(int arc, int triplet, int a, int b) {
  if (auto p = pixel(arc, triplet, a, b); (p > 0 && p <= total_pixels)) {
    pixel_data[p - 1] += 1;
    return true;
  }
  return false;
}


bool bifrost::data::PixelManager::includes(int arc, int triplet, int a, int b) const {
  auto g = group(arc, triplet);
  if (g < 0) return false;

  auto pos = static_cast<double>(a) / static_cast<double>(a + b);
  auto tube = calibration.getUnitId(g, pos);
  if (tube < 0) return false;

  auto unit_pos = calibration.unitPosition(g, tube, pos);
  if (unit_pos < 0 || unit_pos > 1) return false;

//  return calibration.pulseHeightOK(g, tube, a+b);
  return true;
}

int bifrost::data::PixelManager::group(int arc, int triplet) const {
  return arc * triplets + triplet;
}


void bifrost::data::PixelManager::save_to(const hdf5::node::Group & parent) const {
  std::string creator{"fylgje"};
  std::string version{"v0.0.1"};
  std::string instrument{"BIFROST"};

  // create a group for the data
  auto group = parent.create_group("pixels");

  group.attributes.create_from("creator", creator);
  group.attributes.create_from("version", version);
  group.attributes.create_from("instrument", instrument);
  group.attributes.create_from("arcs", arcs);
  group.attributes.create_from("triplets", triplets);
  group.attributes.create_from("tubes", tubes_per_triplet);
  group.attributes.create_from("pixels", pixels_per_tube);

  std::vector<std::string> pixel_order{{"arcs", "tubes", "triplets"}};
  std::vector<std::string> data_order{{"arc"}, {"triplets"}, {"type"}};
  group.attributes.create_from("pixel_order", pixel_order);
  group.attributes.create_from("data_order", data_order);

  // all datasets are integer valued
  auto datatype = hdf5::datatype::create<int>();
  // and we know their final size already, so use contiguous layout
  hdf5::property::DatasetCreationList datasetCreationList;
  datasetCreationList.layout(hdf5::property::DatasetLayout::CONTIGUOUS);

  auto dimensions = hdf5::Dimensions({pixel_data.size()});
  auto dataspace = hdf5::dataspace::Simple(dimensions);
  auto pds = group.create_dataset("data", datatype, dataspace, datasetCreationList);
  pds.attributes.create_from("wrap_order", pixel_order);
  pds.write(pixel_data);
}


void bifrost::data::PixelManager::save_to(const hdf5::file::File & file, const std::optional<std::string> & group) const {
  auto root = file.root();
  std::string name = group.value_or("fylgje");
  if (root.has_group(name)){
    throw std::runtime_error(fmt::format("The provided file already has the group /{}", name));
  }
  auto gr = root.create_group(name);
  save_to(gr);
}


void bifrost::data::PixelManager::save_to(const std::filesystem::path & file, const std::optional<std::string> & group) const{
  namespace fs = std::filesystem;
  auto status = fs::status(file);
  hdf5::file::File hdf5_file;
  if (status.type() == fs::file_type::regular || status.type() == fs::file_type::symlink){
    // then it should be an HDF5 file that we can write to:
    if (!hdf5::file::is_hdf5_file(std::string(file))) {
      throw std::runtime_error(fmt::format("{} is not an HDF5 file", std::string(file)));
    }
    hdf5_file = hdf5::file::open(std::string(file), hdf5::file::AccessFlags::READWRITE);
  } else if (status.type() == fs::file_type::not_found){
    hdf5_file = hdf5::file::create(std::string(file));
  } else {
    throw std::runtime_error(fmt::format("{} exists but is not a file", std::string(file)));
  }

  save_to(hdf5_file, group);
}
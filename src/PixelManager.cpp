// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief fylgje interface to assign pixel IDs like the EFU
//===----------------------------------------------------------------------===//
#include "PixelManager.h"
#include "Version.h"

///\brief Replicate the EFU calculations to identify a unique pixel number
///\returns 0 if no valid pixel
int bifrost::data::PixelManager::pixel(const int arc, const int triplet, const int a, const int b) const {
  const auto g = group(arc, triplet);
  const auto pos = static_cast<double>(a) / static_cast<double>(a + b);
  const auto tube = calibration.getUnitId(g, pos);
  if (tube < 0) {
    // invalid global position (outside any unit's range)
    return 0;
  }
  const auto cor_pos = calibration.posCorrection(g, tube, calibration.unitPosition(g, tube,  pos)); // in range (0, 1)
  // corrected position in (0.0, 1.0) is mapped to a tube pixel in (0, pixels_per_tube - 1)
  // its offset by which tube it is, which triplet its in, and which arc its in
  const int offset = pixels_per_arc * arc + pixels_per_tube_arc * tube + pixels_per_tube * triplet;
  // and note that valid pixels index from 1 -- not 0.
  return 1 + offset + static_cast<int>((pixels_per_tube - 1) * cor_pos);
}


bool bifrost::data::PixelManager::add(const int arc, const int triplet, const int a, const int b) {
  if (const auto p = pixel(arc, triplet, a, b); (p > 0 && p <= total_pixels)) {
    pixel_data[p - 1] += 1;
    return true;
  }
  return false;
}


bool bifrost::data::PixelManager::includes(const int arc, const int triplet, const int a, const int b) const {
  const auto g = group(arc, triplet);
  if (g < 0) return false;

  const auto pos = static_cast<double>(a) / static_cast<double>(a + b);
  const auto tube = calibration.getUnitId(g, pos);
  if (tube < 0) return false;

  if (const auto unit_pos = calibration.unitPosition(g, tube, pos); unit_pos < 0 || unit_pos > 1) return false;

  if (const auto pulse_height = a + b; pulse_height > calibration.pulseHeightUpper()) return false;

  return true;
}

int bifrost::data::PixelManager::group(const int arc, const int triplet) const {
  return arc * triplets + triplet;
}


void bifrost::data::PixelManager::create_in(const hdf5::node::Group & parent) const {
  std::string creator{fylgje::creator};
  std::string version{fylgje::version};
  std::string instrument{"BIFROST"};

  // create a group for the data
  auto pixel_group = parent.create_group("pixels");

  pixel_group.attributes.create_from("creator", creator);
  pixel_group.attributes.create_from("version", version);
  pixel_group.attributes.create_from("instrument", instrument);
  pixel_group.attributes.create_from("arcs", arcs);
  pixel_group.attributes.create_from("triplets", triplets);
  pixel_group.attributes.create_from("tubes", tubes_per_triplet);
  pixel_group.attributes.create_from("pixels", pixels_per_tube);

  std::vector<std::string> pixel_order{{"arcs", "tubes", "triplets"}};
  std::vector<std::string> data_order{{"arc"}, {"triplets"}, {"type"}};
  pixel_group.attributes.create_from("pixel_order", pixel_order);
  pixel_group.attributes.create_from("data_order", data_order);

  // all datasets are integer valued
  auto datatype = hdf5::datatype::create<int>();
  // the size never changes, so use contiguous layout (rewritten in place)
  hdf5::property::DatasetCreationList datasetCreationList;
  datasetCreationList.layout(hdf5::property::DatasetLayout::Contiguous);

  auto dimensions = hdf5::Dimensions({pixel_data.size()});
  auto dataspace = hdf5::dataspace::Simple(dimensions);
  auto pds = pixel_group.create_dataset("data", datatype, dataspace, datasetCreationList);
  pds.attributes.create_from("wrap_order", pixel_order);
}

void bifrost::data::PixelManager::write_to(const hdf5::node::Group & parent) const {
  write_to(parent, pixel_data);
}

void bifrost::data::PixelManager::write_to(const hdf5::node::Group & parent, const data_t & snapshot) {
  parent.get_group("pixels").get_dataset("data").write(snapshot);
}

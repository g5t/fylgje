// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief fylgje interface to handle events from AR51 format messages
//===----------------------------------------------------------------------===//
#include "EventManager.h"


bool bifrost::data::EventManager::add(const bifrost::message_t & message){
  if (store_events) {
    if (messages.capacity() - messages.size() < 1) {
      messages.reserve(messages.capacity() ? 10 * messages.capacity() : 10000u);
    }
    messages.push_back(message);
  }

  auto arc_ = bifrost::arc(message.group);
  auto triplet_ = bifrost::triplet(message.fiber, message.group);
  auto allowed = pixel_manager.includes(arc_, triplet_, message.a, message.b);
  if (allowed){
    pixel_manager.add(arc_, triplet_, message.a, message.b);
  }
  return !histogram_manager.has_value() || histogram_manager->add(message, allowed);
}

bool bifrost::data::EventManager::add(int fiber, int group, int a, int b, double time, uint32_t high, uint32_t low){
  return add({fiber, group, a, b, time, high, low});
}


void bifrost::data::EventManager::save_to(const hdf5::node::Group & parent) const {
  std::string creator{"fylgje"};
  std::string version{"v0.0.1"};
  std::string instrument{"BIFROST"};

  // create a group for the data
  auto group = parent.create_group("events");

  group.attributes.create_from("creator", creator);
  group.attributes.create_from("version", version);
  group.attributes.create_from("instrument", instrument);
  // and the stored messages
  auto dimensions = hdf5::Dimensions({messages.size()});
  auto message_dataspace = hdf5::dataspace::Simple(dimensions);
  auto compound = bifrost::message_type();
  // and we know their final size already, so use contiguous layout
  hdf5::property::DatasetCreationList datasetCreationList;
  datasetCreationList.layout(hdf5::property::DatasetLayout::Contiguous);

  if (store_events) {
    auto message_dataset = group.create_dataset("messages", compound, message_dataspace, datasetCreationList);
    message_dataset.write(messages);
  }
  if (histogram_manager.has_value()) {
    histogram_manager->save_to(parent);
  }
  if (store_pixels) {
    pixel_manager.save_to(parent);
  }

  std::cout << "Saved data including " << messages.size() << " readouts to HDF5 file\n";
}

void bifrost::data::EventManager::save_to(const hdf5::file::File & file, const std::optional<std::string> & group) const {
  auto root = file.root();
  std::string name = group.value_or("fylgje");
  if (root.has_group(name)){
    throw std::runtime_error(fmt::format("The provided file already has the group /{}", name));
  }
  auto gr = root.create_group(name);
  save_to(gr);
}

void bifrost::data::EventManager::save_to(const std::filesystem::path & file, const std::optional<std::string> & group) const{
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

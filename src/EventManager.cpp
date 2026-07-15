// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief fylgje interface to handle events from AR51 format messages
//===----------------------------------------------------------------------===//
#include "EventManager.h"
#include "Version.h"


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
  std::string creator{fylgje::creator};
  std::string version{fylgje::version};
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


void bifrost::data::EventManager::open_file(const std::filesystem::path & file, const std::optional<std::string> & group, const bool swmr){
  namespace fs = std::filesystem;
  if (out_file.has_value()){
    throw std::runtime_error("A file is already open for incremental writing");
  }
  // SWMR requires the most recent HDF5 file format
  hdf5::property::FileAccessList fapl;
  if (swmr) {
    fapl.library_version_bounds(hdf5::property::LibVersion::Latest, hdf5::property::LibVersion::Latest);
  }

  auto status = fs::status(file);
  hdf5::file::File hdf5_file;
  if (status.type() == fs::file_type::regular || status.type() == fs::file_type::symlink){
    if (!hdf5::file::is_hdf5_file(std::string(file))) {
      throw std::runtime_error(fmt::format("{} is not an HDF5 file", std::string(file)));
    }
    hdf5_file = hdf5::file::open(std::string(file), hdf5::file::AccessFlags::ReadWrite, fapl);
  } else if (status.type() == fs::file_type::not_found){
    hdf5_file = hdf5::file::create(std::string(file), hdf5::file::AccessFlags::Exclusive,
                                   hdf5::property::FileCreationList(), fapl);
  } else {
    throw std::runtime_error(fmt::format("{} exists but is not a file", std::string(file)));
  }

  const auto name = group.value_or("fylgje");
  if (hdf5_file.root().has_group(name)){
    throw std::runtime_error(fmt::format("The provided file already has the group /{}", name));
  }

  // SWMR mode forbids creating objects, so the COMPLETE structure is made here
  {
    auto gr = hdf5_file.root().create_group(name);
    auto events = gr.create_group("events");
    events.attributes.create_from("creator", std::string{fylgje::creator});
    events.attributes.create_from("version", std::string{fylgje::version});
    events.attributes.create_from("instrument", std::string{"BIFROST"});
    if (store_events) {
      // appendable: chunked with unlimited extent, grown at each flush
      auto dataspace = hdf5::dataspace::Simple({0}, {hdf5::dataspace::Simple::unlimited});
      hdf5::property::DatasetCreationList dcpl;
      dcpl.layout(hdf5::property::DatasetLayout::Chunked);
      dcpl.chunk({4096});
      events.create_dataset("messages", bifrost::message_type(), dataspace, dcpl);
    }
    if (histogram_manager.has_value()) {
      histogram_manager->create_in(gr);
    }
    if (store_pixels) {
      pixel_manager.create_in(gr);
    }
  }

  if (swmr) {
    // structure is complete: switch to SWMR so other processes may read along
    hdf5_file.close();
    hdf5_file = hdf5::file::open(std::string(file),
                                 hdf5::file::AccessFlags::ReadWrite | hdf5::file::AccessFlags::SWMRWrite,
                                 fapl);
  }

  out_file = hdf5_file;
  out_group = hdf5_file.root().get_group(name);
  if (store_events) {
    out_messages = out_group->get_group("events").get_dataset("messages");
  }
  messages_written = 0;
}


void bifrost::data::EventManager::flush(){
  if (!out_file.has_value()){
    throw std::runtime_error("No file open for incremental writing");
  }
  if (store_events && out_messages.has_value() && !messages.empty()){
    const auto total = messages_written + messages.size();
    out_messages->resize({total});
    const hdf5::dataspace::Hyperslab slab({messages_written}, {messages.size()});
    out_messages->write(messages, slab);
    messages_written = total;
    // appended to file: drop from memory (capacity is retained)
    messages.clear();
  }
  if (histogram_manager.has_value()){
    histogram_manager->write_to(*out_group);
  }
  if (store_pixels){
    pixel_manager.write_to(*out_group);
  }
  out_file->flush(hdf5::file::Scope::Global);
}


void bifrost::data::EventManager::close_file(){
  if (!out_file.has_value()){
    return;
  }
  flush();
  std::cout << "Saved data including " << messages_written << " readouts to HDF5 file\n";
  out_messages.reset();
  out_group.reset();
  out_file->close();
  out_file.reset();
}

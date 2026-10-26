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

  const auto arc_ = bifrost::arc(message.group);
  const auto triplet_ = bifrost::triplet(message.fiber, message.group);
  const auto allowed = pixel_manager.includes(arc_, triplet_, message.a, message.b);
  if (allowed){
    pixel_manager.add(arc_, triplet_, message.a, message.b);
  }
  return !histogram_manager.has_value() || histogram_manager->add(message, allowed);
}

bool bifrost::data::EventManager::add(const int fiber, const int group, const int a, const int b, const double time, const uint32_t high, const uint32_t low){
  return add({fiber, group, a, b, time, high, low});
}


void bifrost::data::EventManager::open_file(const std::filesystem::path & file, const std::optional<std::string> & group, const bool swmr){
  namespace fs = std::filesystem;
  if (out_file.has_value()){
    throw std::runtime_error("A file is already open for incremental writing");
  }
  // SWMR requires the most recent HDF5 file format
  HighFive::FileAccessProps fapl;
  if (swmr) {
    fapl.add(HighFive::FileVersionBounds(H5F_LIBVER_LATEST, H5F_LIBVER_LATEST));
  }

  const auto status = fs::status(file);
  const auto exists = status.type() == fs::file_type::regular || status.type() == fs::file_type::symlink;
  if (exists && H5Fis_accessible(file.c_str(), H5P_DEFAULT) <= 0) {
    throw std::runtime_error(fmt::format("{} is not an HDF5 file", std::string(file)));
  }
  if (!exists && status.type() != fs::file_type::not_found){
    throw std::runtime_error(fmt::format("{} exists but is not a file", std::string(file)));
  }

  const auto name = group.value_or("fylgje");
  // SWMR mode forbids creating objects, so the COMPLETE structure is made
  // here; leaving the scope closes every handle, and with them the file
  {
    HighFive::File hdf5_file(file, exists ? HighFive::File::ReadWrite : HighFive::File::Excl, fapl);
    if (hdf5_file.exist(name)){
      throw std::runtime_error(fmt::format("The provided file already has the group /{}", name));
    }
    auto gr = hdf5_file.createGroup(name);
    auto events = gr.createGroup("events");
    events.createAttribute("creator", std::string{fylgje::creator});
    events.createAttribute("version", std::string{fylgje::version});
    events.createAttribute("instrument", std::string{"BIFROST"});
    if (store_events) {
      // appendable: chunked with unlimited extent, grown at each flush
      const HighFive::DataSpace dataspace({0}, {HighFive::DataSpace::UNLIMITED});
      HighFive::DataSetCreateProps dcpl;
      dcpl.add(HighFive::Chunking(std::vector<hsize_t>{4096}));
      events.createDataSet("messages", dataspace, bifrost::message_type(), dcpl);
    }
    if (histogram_manager.has_value()) {
      histogram_manager->create_in(gr);
    }
    if (store_pixels) {
      pixel_manager.create_in(gr);
    }
  }

  // structure is complete: with SWMR, other processes may now read along
  out_file.emplace(file, swmr ? HighFive::File::ReadWrite | HighFive::File::WriteSWMR : HighFive::File::ReadWrite, fapl);
  out_group = out_file->getGroup(name);
  if (store_events) {
    out_messages = out_group->getGroup("events").getDataSet("messages");
  }
  messages_written = 0;
}


std::vector<bifrost::data::EventManager::message_t> bifrost::data::EventManager::take_messages(){
  std::vector<message_t> taken;
  taken.swap(messages);
  return taken;
}

void bifrost::data::EventManager::write_events(const std::vector<message_t> & batch){
  if (!out_file.has_value()){
    throw std::runtime_error("No file open for incremental writing");
  }
  if (!store_events || !out_messages.has_value() || batch.empty()){
    return;
  }
  const auto total = messages_written + batch.size();
  out_messages->resize({total});
  out_messages->select({messages_written}, {batch.size()}).write(batch);
  messages_written = total;
}

void bifrost::data::EventManager::write_pixels(const PixelManager::data_t & snapshot) const {
  if (!out_file.has_value()){
    throw std::runtime_error("No file open for incremental writing");
  }
  if (store_pixels && !snapshot.empty()){
    pixel_manager.write_to(*out_group, snapshot);
  }
}

void bifrost::data::EventManager::file_flush() {
  if (!out_file.has_value()){
    throw std::runtime_error("No file open for incremental writing");
  }
  out_file->flush();
}

void bifrost::data::EventManager::flush(){
  if (!out_file.has_value()){
    throw std::runtime_error("No file open for incremental writing");
  }
  write_events(messages);
  // appended to file: drop from memory (capacity is retained)
  messages.clear();
  if (histogram_manager.has_value()){
    histogram_manager->write_to(*out_group);
  }
  if (store_pixels){
    pixel_manager.write_to(*out_group);
  }
  file_flush();
}


void bifrost::data::EventManager::close_file(){
  if (!out_file.has_value()){
    return;
  }
  flush();
  std::cout << "Saved data including " << messages_written << " readouts to HDF5 file\n";
  // the file closes with its last handle
  out_messages.reset();
  out_group.reset();
  out_file.reset();
}

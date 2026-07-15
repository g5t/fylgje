// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief Background HDF5 writer overlapping file output with consumption
//===----------------------------------------------------------------------===//
#include <algorithm>
#include "AsyncWriter.h"

using bifrost::data::AsyncWriter;

AsyncWriter::AsyncWriter(EventManager & data, std::atomic<bool> * stop, const std::size_t max_outstanding)
    : data{data}, stop_flag{stop}, max_outstanding{std::max<std::size_t>(1, max_outstanding)},
      worker{&AsyncWriter::loop, this} {}

AsyncWriter::~AsyncWriter(){
  try {
    finish();
  } catch (...) {
    // a write error is only reportable through finish(); call it explicitly
  }
}

void AsyncWriter::enqueue(Batch batch){
  const auto wait_start = clock_t::now();
  std::unique_lock lock(mutex);
  space_cv.wait(lock, [this]{ return queue.size() < max_outstanding || error || stopping; });
  stall_time += clock_t::now() - wait_start;
  if (error || stopping){
    // a failed or finishing writer accepts no more work; finish() reports
    return;
  }
  queue.push_back(std::move(batch));
  work_cv.notify_one();
}

void AsyncWriter::finish(){
  {
    std::lock_guard lock(mutex);
    stopping = true;
  }
  work_cv.notify_all();
  space_cv.notify_all();
  if (worker.joinable()){
    worker.join();
  }
  // the thread is gone: no lock needed, and report each error only once
  if (error){
    auto to_throw = error;
    error = nullptr;
    std::rethrow_exception(to_throw);
  }
}

void AsyncWriter::loop(){
  for (;;) {
    Batch batch;
    {
      std::unique_lock lock(mutex);
      work_cv.wait(lock, [this]{ return !queue.empty() || stopping; });
      if (queue.empty()){
        return; // stopping, and everything queued has been written
      }
      batch = std::move(queue.front());
      queue.pop_front();
      writing = true;
    }
    space_cv.notify_one();
    const auto write_start = clock_t::now();
    try {
      data.write_events(batch.events);
      data.write_pixels(batch.pixels);
      data.file_flush();
    } catch (...) {
      std::lock_guard lock(mutex);
      error = std::current_exception();
      queue.clear();
      writing = false;
      if (stop_flag != nullptr){
        stop_flag->store(true);
      }
      space_cv.notify_all();
      return;
    }
    const auto write_end = clock_t::now();
    {
      std::lock_guard lock(mutex);
      write_time += write_end - write_start;
      ++flush_count;
      last_write_at = write_end;
      writing = false;
    }
  }
}

std::size_t AsyncWriter::pending_batches() const {
  std::lock_guard lock(mutex);
  return queue.size() + (writing ? 1 : 0);
}

int64_t AsyncWriter::flushes() const {
  std::lock_guard lock(mutex);
  return flush_count;
}

AsyncWriter::clock_t::duration AsyncWriter::write_duration() const {
  std::lock_guard lock(mutex);
  return write_time;
}

AsyncWriter::clock_t::duration AsyncWriter::stall_duration() const {
  std::lock_guard lock(mutex);
  return stall_time;
}

std::optional<AsyncWriter::clock_t::duration> AsyncWriter::since_last_write() const {
  std::lock_guard lock(mutex);
  if (!last_write_at.has_value()){
    return std::nullopt;
  }
  return clock_t::now() - last_write_at.value();
}

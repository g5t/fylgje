// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief Background HDF5 writer overlapping file output with consumption
///
/// The consuming thread hands over event batches with enqueue(); a single
/// internal thread performs every HDF5 write through the EventManager's
/// write_events/write_pixels/file_flush. The linked HDF5 build is not
/// thread-safe, so between construction and finish() nothing else may touch
/// the EventManager's open file (flush/close_file included).
//===----------------------------------------------------------------------===//
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>
#include "EventManager.h"

namespace bifrost::data {
  class AsyncWriter {
  public:
    using message_t = EventManager::message_t;
    using clock_t = std::chrono::steady_clock;
    struct Batch {
      std::vector<message_t> events;
      /// pixel counts snapshot (taken on the consuming thread; empty = skip)
      PixelManager::data_t pixels;
    };

    /// \param data manager whose incremental file is already open (open_file)
    /// \param stop optional flag raised when a write fails, so a consumer
    ///        polling it halts promptly; the failure itself is rethrown by finish()
    /// \param max_outstanding backpressure: enqueue() blocks while this many
    ///        batches await writing, bounding event memory to roughly
    ///        max_outstanding batches plus the one being accumulated
    explicit AsyncWriter(EventManager & data, std::atomic<bool> * stop = nullptr,
                         std::size_t max_outstanding = 2);
    /// \brief joins the thread; call finish() first to observe write errors
    ~AsyncWriter();
    AsyncWriter(const AsyncWriter &) = delete;
    AsyncWriter & operator=(const AsyncWriter &) = delete;

    /// \brief hand a batch to the writer thread (consuming thread only);
    ///        blocks while max_outstanding batches are already queued
    void enqueue(Batch batch);
    /// \brief write all queued batches, stop the thread, and rethrow any
    ///        write error; idempotent, after which the EventManager may be
    ///        used from the calling thread again
    void finish();

    /// \brief batches not yet fully written (queued plus any in progress)
    [[nodiscard]] std::size_t pending_batches() const;
    /// \brief completed batch writes
    [[nodiscard]] int64_t flushes() const;
    /// \brief time the writer thread has spent writing
    [[nodiscard]] clock_t::duration write_duration() const;
    /// \brief time enqueue() has spent blocked on backpressure
    [[nodiscard]] clock_t::duration stall_duration() const;
    /// \brief time since a batch write last completed; nullopt before the first
    [[nodiscard]] std::optional<clock_t::duration> since_last_write() const;

  private:
    void loop();

    EventManager & data;
    std::atomic<bool> * stop_flag;
    std::size_t max_outstanding;
    mutable std::mutex mutex;
    std::condition_variable work_cv;   ///< writer waits here for batches
    std::condition_variable space_cv;  ///< enqueue waits here for queue space
    std::deque<Batch> queue;
    bool writing{false}; ///< a popped batch is being written right now
    bool stopping{false};
    std::exception_ptr error;
    int64_t flush_count{0};
    clock_t::duration write_time{0};
    clock_t::duration stall_time{0};
    std::optional<clock_t::time_point> last_write_at;
    std::thread worker; ///< last member: started after everything else exists
  };
}

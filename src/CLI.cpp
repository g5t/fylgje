// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief CLI application entrypoint for fylgje
//===----------------------------------------------------------------------===//
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <memory>
#include <string>
#include <unistd.h>

#include "App.h"
#include "CLI.h"

#include "AsyncWriter.h"
#include "Bytes.h"
#include "ESSConsumer.h"
#include "EventManager.h"
#include "HistogramManager.h"
#include "PixelManager.h"

namespace {
  std::atomic<bool> cli_interrupted{false};
}

/// \brief SIGINT: request a graceful stop; a second Ctrl-C terminates immediately
extern "C" void fylgje_cli_on_sigint(int) {
  cli_interrupted.store(true);
  std::signal(SIGINT, SIG_DFL);
}

int fylgje_app_cli(
    Configuration & configuration,
    Calibration & calibration,
    kafka::time::milliseconds from,
    std::optional<kafka::time::milliseconds> to,
    const std::optional<std::string> & output_file,
    bool store_events,
    bool store_pixels,
    bool store_histograms,
    kafka::time::milliseconds write_every,
    bool verbose,
    int64_t write_buffer
  ) {
  using namespace bifrost::data;
  auto tubes = configuration.Instrument.units_per_group;
  auto pixelation = configuration.Instrument.pixels_per_unit;
  int arcs{5}, triplets{9};

  std::shared_ptr<EventManager> data;
  if (store_histograms) {
    data = std::make_shared<EventManager>(
        PixelManager(arcs, triplets, tubes, pixelation, calibration),
        HistogramManager(arcs ,triplets, calibration)
        );
  } else {
    data = std::make_shared<EventManager>(PixelManager(arcs, triplets, tubes, pixelation, calibration));
  }
  data->storePixels(store_pixels);
  data->storeEvents(store_events);

  std::unique_ptr<ESSConsumer<EventManager>> worker;
  if (to.has_value()){
    worker = std::make_unique<ESSConsumer<EventManager>>(data, configuration, from, to.value());
  } else {
    fmt::print("No end time provided; consuming until interrupted (Ctrl-C to stop and save)\n");
    worker = std::make_unique<ESSConsumer<EventManager>>(data, configuration, from);
  }

  // open (and structure) the output file up front: any path problem surfaces
  // now instead of after the consumption has finished
  const bool periodic = write_every.count() > 0 || write_buffer > 0;
  data->open_file(output_file.value_or("fylgje.h5"), std::nullopt, periodic);
  std::optional<bifrost::data::AsyncWriter> writer;
  if (periodic) {
    if (write_buffer > 0 && write_every.count() > 0) {
      fmt::print("Writing to file when buffered events exceed {} or every {}\n",
                 fylgje::bytes_to_size_string(write_buffer),
                 kafka::time::milliseconds_to_duration_string(write_every));
    } else if (write_buffer > 0) {
      fmt::print("Writing to file when buffered events exceed {}\n",
                 fylgje::bytes_to_size_string(write_buffer));
    } else {
      fmt::print("Writing to file every {}\n",
                 kafka::time::milliseconds_to_duration_string(write_every));
    }
    if (store_histograms) {
      fmt::print("Histogram data is written once at exit\n");
    }
    // writes happen on the AsyncWriter's thread, overlapping consumption; the
    // periodic callback only checks the triggers and hands batches over (its
    // duration, including any backpressure wait, is excluded from the
    // consumer's idle budget). Until finish(), the writer thread is the only
    // one allowed to touch the open HDF5 file.
    writer.emplace(*data, &cli_interrupted);
    worker->setPeriodicCallback(
        std::chrono::milliseconds{100},
        [&data, &writer, write_buffer, write_every, store_pixels,
         last_handoff = std::chrono::steady_clock::now()]() mutable {
          const auto now = std::chrono::steady_clock::now();
          const bool by_size = write_buffer > 0
              && data->buffered_bytes() >= static_cast<size_t>(write_buffer);
          const bool by_time = write_every.count() > 0 && now - last_handoff >= write_every;
          if (!by_size && !by_time) {
            return;
          }
          writer->enqueue({data->take_messages(),
                           store_pixels ? data->pixel_snapshot() : bifrost::data::PixelManager::data_t{}});
          last_handoff = std::chrono::steady_clock::now();
        });
  } else {
    fmt::print("Periodic writing disabled; the file is written once at exit\n");
  }

  // live progress feedback on stderr (stdout stays machine-readable): a
  // single '\r'-rewritten line on a terminal, an occasional plain line when
  // redirected. progress_line_open outlives run(): the consumer holds
  // references to it through the registered callbacks.
  bool progress_line_open{false};
  const auto duration_string = [](std::chrono::steady_clock::duration d) -> std::string {
    const auto ms = std::chrono::duration_cast<kafka::time::milliseconds>(d);
    if (ms.count() == 0 && d > std::chrono::steady_clock::duration::zero()) {
      return "<1ms";
    }
    return kafka::time::milliseconds_to_duration_string(ms);
  };
  if (verbose) {
    const bool tty = isatty(fileno(stderr)) != 0;
    auto progress = [&worker, &writer, &progress_line_open, duration_string, tty,
                     run_start = std::chrono::steady_clock::now(),
                     last_length = std::size_t{0}]() mutable {
      auto line = fmt::format("consuming...  elapsed {} | {} messages | {} readouts",
                              duration_string(std::chrono::steady_clock::now() - run_start),
                              worker->message_count(), worker->event_count());
      if (writer.has_value()) {
        if (const auto since = writer->since_last_write(); since.has_value()) {
          line += fmt::format(" | last write {} ago", duration_string(since.value()));
        }
      }
      if (tty) {
        // pad over any leftover tail of a previously longer line
        const auto pad = line.size() < last_length ? std::string(last_length - line.size(), ' ') : std::string{};
        fmt::print(stderr, "\r{}{}", line, pad);
        std::fflush(stderr);
        last_length = line.size();
        progress_line_open = true;
      } else {
        fmt::print(stderr, "{}\n", line);
      }
    };
    auto line_break = [&progress_line_open] {
      if (progress_line_open) {
        std::fputc('\n', stderr);
        progress_line_open = false;
      }
    };
    worker->setProgressCallback(tty ? std::chrono::seconds{1} : std::chrono::seconds{10},
                                std::move(progress), std::move(line_break));
  }

  cli_interrupted.store(false);
  worker->setStopFlag(&cli_interrupted);
  const auto previous_handler = std::signal(SIGINT, fylgje_cli_on_sigint);
  worker->run();
  std::signal(SIGINT, previous_handler == SIG_ERR ? SIG_DFL : previous_handler);
  if (cli_interrupted.load()) {
    fmt::print("Interrupted; saving collected data\n");
  }

  // machine-readable summary, relied upon by the integration test harness;
  // printed before the writer drains so the counts appear promptly
  fmt::print("messages_consumed={}\n", worker->message_count());
  fmt::print("readouts_processed={}\n", worker->event_count());

  int exit_code{0};
  if (writer.has_value()) {
    // say how much output work remains when it will take noticeable time:
    // with large --write-buffer values the drain plus the final tail write
    // can run for a while after consumption ends
    if (const auto queued = writer->pending_batches();
        queued > 0 || data->buffered_bytes() >= (size_t{64} << 20)) {
      fmt::print("Writing remaining data to file ({} queued batch(es) and {} buffered)...\n",
                 queued, fylgje::bytes_to_size_string(static_cast<int64_t>(data->buffered_bytes())));
    }
    try {
      // drain queued batches and stop the thread; the EventManager is ours
      // again afterwards (close_file below writes the tail + histograms)
      writer->finish();
    } catch (const std::exception & e) {
      fmt::print("Writing failed: {}\n", e.what());
      exit_code = 1;
    }
  }
  const auto close_start = std::chrono::steady_clock::now();
  try {
    data->close_file();
  } catch (const std::exception & e) {
    fmt::print("Failed to finalise the output file: {}\n", e.what());
    exit_code = 1;
  }
  if (verbose) {
    const auto close_time = std::chrono::steady_clock::now() - close_start;
    const auto write_time = (writer.has_value() ? writer->write_duration()
                                                : std::chrono::steady_clock::duration{0}) + close_time;
    const auto flushes = (writer.has_value() ? writer->flushes() : 0) + 1; // + the final write during close
    auto line = fmt::format("timing: consuming {}, writing {} ({} flushes)",
                            duration_string(worker->consume_duration()),
                            duration_string(write_time), flushes);
    if (writer.has_value() && writer->stall_duration() >= std::chrono::milliseconds{1}) {
      line += fmt::format(", consumer stalled {}", duration_string(writer->stall_duration()));
    }
    fmt::print(stderr, "{}\n", line);
  }
  return exit_code;
}
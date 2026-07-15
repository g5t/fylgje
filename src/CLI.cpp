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
    bool verbose
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
  const bool periodic = write_every.count() > 0;
  data->open_file(output_file.value_or("fylgje.h5"), std::nullopt, periodic);
  if (periodic) {
    fmt::print("Writing collected data to file every {} s\n", write_every.count() / 1000.0);
    worker->setPeriodicCallback(std::chrono::milliseconds{write_every.count()}, [&data]{ data->flush(); });
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
    auto progress = [&worker, &progress_line_open, duration_string, tty,
                     run_start = std::chrono::steady_clock::now(),
                     last_length = std::size_t{0}]() mutable {
      auto line = fmt::format("consuming...  elapsed {} | {} messages | {} readouts",
                              duration_string(std::chrono::steady_clock::now() - run_start),
                              worker->message_count(), worker->event_count());
      if (const auto since = worker->since_last_write(); since.has_value()) {
        line += fmt::format(" | last write {} ago", duration_string(since.value()));
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

  // machine-readable summary, relied upon by the integration test harness
  fmt::print("messages_consumed={}\n", worker->message_count());
  fmt::print("readouts_processed={}\n", worker->event_count());
  const auto close_start = std::chrono::steady_clock::now();
  data->close_file();
  if (verbose) {
    const auto close_time = std::chrono::steady_clock::now() - close_start;
    fmt::print(stderr, "timing: consuming {}, writing {} ({} flushes)\n",
               duration_string(worker->consume_duration()),
               duration_string(worker->write_duration() + close_time),
               worker->flushes() + 1); // + the final write during close
  }
  return 0;
}

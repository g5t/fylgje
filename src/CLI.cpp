// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief CLI application entrypoint for fylgje
//===----------------------------------------------------------------------===//
#include <atomic>
#include <csignal>
#include <memory>

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
    kafka::time::milliseconds write_every
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
    worker->setPeriodicCallback(std::chrono::milliseconds{write_every.count()}, [&data]{ data->flush(); });
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
  data->close_file();
  return 0;
}
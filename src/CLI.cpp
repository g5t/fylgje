// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief CLI application entrypoint for fylgje
//===----------------------------------------------------------------------===//
#include "App.h"
#include "CLI.h"

#include "ESSConsumer.h"
#include "EventManager.h"
#include "HistogramManager.h"
#include "PixelManager.h"

int fylgje_app_cli(
    Configuration & configuration,
    Calibration & calibration,
    kafka::time::milliseconds from,
    std::optional<kafka::time::milliseconds> to,
    const std::optional<std::string> & output_file,
    bool store_events,
    bool store_pixels,
    bool store_histograms
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

  // TODO Instead of this, consume forever but put in a CTRL-C handler to
  //      stop the consumer and save the data before exiting.
  if (!to.has_value()){
    fmt::print("<<<<\n WARNING No end time provided, using now \n>>>>\n");
    to = kafka::time::time_t_to_milliseconds(kafka::time::now_to_time_t());
  }
  ESSConsumer<EventManager> worker{data, configuration, from, to.value()};
  worker.run();
  data->save_to(output_file.value_or("fylgje.h5"));
  return 0;
}
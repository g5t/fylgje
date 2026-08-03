// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
//
/// \file
//
/// \brief fylgje application entrypoint for CLI and GUI
///
/// The non-GUI version does not depend on any Qt libraries and can be run
/// headless as a result, it only has a CLI application.
/// The GUI version uses Qt and offers a full GUI application or a CLI application.
//===----------------------------------------------------------------------===//
#include <iostream>
#include "App.h"
#include "CLI.h"
#ifdef FYLGJE_GUI
#include "GUI.h"
#endif

int fylgjeApp(
    Configuration & configuration,
    Calibration & calibration,
    const kafka::time::milliseconds from,
    const std::optional<kafka::time::milliseconds> to,
    const std::optional<std::string> & output_file,
    const bool gui,
    const bool store_events,
    const bool store_pixels,
    const bool store_histograms,
    const kafka::time::milliseconds write_every,
    const bool verbose,
    const int64_t write_buffer
){
  if (gui){
#ifdef FYLGJE_GUI
    return fylgjeAppGui(configuration, calibration, from, to, output_file, store_events, store_pixels);
#else
    std::cerr << "GUI not available, using CLI instead" << std::endl;
#endif
  }
  return fylgjeAppCli(configuration, calibration, from, to, output_file,
                        store_events, store_pixels, store_histograms, write_every, verbose, write_buffer);
}
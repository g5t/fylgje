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

int fylgje_app(
    Configuration & configuration,
    Calibration & calibration,
    kafka::time::milliseconds from,
    std::optional<kafka::time::milliseconds> to,
    const std::optional<std::string> & output_file,
    bool gui,
    bool store_events,
    bool store_pixels,
    bool store_histograms
){
  if (gui){
#ifdef FYLGJE_GUI
    return fylgje_app_gui(configuration, calibration, from, to, output_file, store_events, store_pixels);
#else
    std::cerr << "GUI not available, using CLI instead" << std::endl;
#endif
  }
  return fylgje_app_cli(configuration, calibration, from, to, output_file,
                        store_events, store_pixels, store_histograms);
}
// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief GUI application entrypoint for fylgje
//===----------------------------------------------------------------------===//
#include "App.h"
#include "GUI.h"

#include <QApplication>
#include "AppWindow.h"

int fylgje_app_gui(
    Configuration & configuration,
    Calibration & calibration,
    kafka::time::milliseconds from,
    std::optional<kafka::time::milliseconds> to,
    const std::optional<std::string> & output_file,
    bool store_events,
    bool store_pixels
){
  int argc{1};
  std::string app_name{"fylgje"};
  char * argv[] = {app_name.data(), nullptr};
  QApplication app(argc, argv);

  MainWindow window(configuration, calibration, from, to, output_file, store_events, store_pixels);
  window.setWindowTitle(QString::fromStdString(configuration.Plot.WindowTitle));
  window.resize(configuration.Plot.Width, configuration.Plot.Height);
  window.show();
  return app.exec();
}
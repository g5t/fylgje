// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief GUI application entrypoint for fylgje
//===----------------------------------------------------------------------===//
#pragma once

int fylgje_app_gui(
    Configuration & configuration,
    Calibration & calibration,
    kafka::time::milliseconds from,
    std::optional<kafka::time::milliseconds> to = std::nullopt,
    const std::optional<std::string> & output_file = std::nullopt,
    bool store_events = false,
    bool store_pixels = false
);

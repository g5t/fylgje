// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
//
/// \file
//
/// \brief fylgje application entrypoint for CLI and GUI variants
//===----------------------------------------------------------------------===//
#pragma once
#include "Configuration.h"
#include "Calibration.h"
#include "Time.h"

/// \brief Run fylgje, either as the Qt GUI or headless on the CLI
///
/// Consumes AR51 messages (presently carrying CAEN-format events) from Kafka
/// between `from` and `to` (or until interrupted, if `to` is unset),
/// optionally writing events, pixels, and/or histograms to `output_file` as
/// they accumulate.
/// \param configuration Instrument configuration information
/// \param calibration Instrument calibration used to pixelate events
/// \param from Starting time for accumulation
/// \param to Ending time for accumulation, or std::nullopt to consume forever
/// \param output_file Optional filename for output of selected data
/// \param gui Launch the Qt GUI instead of running headless on the CLI;
///            unavailable when built without Qt (see `fylgje-cli`)
/// \param store_events True to save raw decoded events to the output file
/// \param store_pixels True to save pixelated information to the output file
/// \param store_histograms True to save processed histograms to the output file
/// \param write_every Ensure that buffered data is written to the file at least this often (in msec)
/// \param verbose True to make the consumer loop produce verbose output
/// \param write_buffer Maximum buffer size in bytes before writing occurs
int fylgjeApp(
    Configuration & configuration,
    Calibration & calibration,
    kafka::time::milliseconds from,
    std::optional<kafka::time::milliseconds> to = std::nullopt,
    const std::optional<std::string> & output_file = std::nullopt,
    bool gui = false,
    bool store_events = false,
    bool store_pixels = false,
    bool store_histograms = false,
    kafka::time::milliseconds write_every = kafka::time::milliseconds{60'000},
    bool verbose = false,
    int64_t write_buffer = int64_t{2} << 30
);

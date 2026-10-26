// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief CLI application entrypoint for fylgje
//===----------------------------------------------------------------------===//
#pragma once
#include "Configuration.h"
#include "Calibration.h"
#include "KafkaTime.h"

/// \brief Run fylgje headless: no Qt dependency, so it works over a plain (non-X-forwarded) ssh connection
///
/// Consumes AR51 messages (presently carrying CAEN-format events) from Kafka
/// between `from` and `to` (or until interrupted, if `to` is unset),
/// optionally writing events, pixels, and/or histograms to `output_file` as
/// they accumulate.
int fylgjeAppCli(
    Configuration & configuration,
    Calibration & calibration,
    kafka::time::milliseconds from,
    std::optional<kafka::time::milliseconds> to = std::nullopt,
    const std::optional<std::string> & output_file = std::nullopt,
    bool store_events = false,
    bool store_pixels = false,
    bool store_histograms = false,
    kafka::time::milliseconds write_every = kafka::time::milliseconds{60'000},
    bool verbose = false,
    int64_t write_buffer = int64_t{2} << 30
);

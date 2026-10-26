# fylgje

Headless capture of ESS raw detector readout (`ar51` flatbuffers on Kafka) — the data
*before* the Event Formation Unit — for 3He position-sensitive-detector instruments such as
BIFROST. `fylgje-cli` consumes a time window (or runs until Ctrl-C), decodes CAEN readouts,
applies a calibration, and writes events, pixels and/or histograms to an HDF5 file. With
incremental writing enabled the file is written in SWMR mode, so it can be inspected while
capture continues.

Extracted, with history, from ECDC's
[daqlite](https://gitlab.esss.lu.se/ecdc/ess-dmsc/daqlite) `src/fylgje`; the Qt GUI was
left behind.

## Build

Dependencies: a C++17 compiler, CMake, librdkafka, HighFive (HDF5), fmt, nlohmann_json,
flatbuffers (`flatc` generates the ar51 header from `schemas/`), and GTest for the tests.
All are on conda-forge:

```sh
micromamba create -p ./env -c conda-forge cmake ninja cxx-compiler librdkafka highfive zlib fmt nlohmann_json flatbuffers gtest
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=$PWD/env
cmake --build build
```

Or, in that environment, `pip install .` puts `fylgje-cli` on `PATH`.

## Use

```sh
fylgje-cli -b HOST:PORT -t bifrost_detector_samples -c calibration.json --duration 90m -e -o run.h5
fylgje-cli -h   # every option, time and duration formats
```

`examples/fylgje.json` is a BIFROST configuration for `-f`.

## Test

```sh
ctest --test-dir build -L mock          # in-process mock Kafka cluster
ctest --test-dir build -L integration   # ephemeral podman Kafka; needs click and python-on-whales
```

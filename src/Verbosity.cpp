// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief Global output-verbosity level
//===----------------------------------------------------------------------===//
#include <atomic>
#include "Verbosity.h"

namespace {
  std::atomic<int> verbosity_level{0};
}

void fylgje::log::set_verbosity(const int level) {
  verbosity_level.store(level);
}

int fylgje::log::verbosity() {
  return verbosity_level.load();
}

bool fylgje::log::debug() {
  return verbosity_level.load() >= 2;
}

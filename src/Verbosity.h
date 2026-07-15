// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief Global output-verbosity level, set once at startup from the
///        counted -v occurrences: 0 = essential output only, 1 (-v) = live
///        progress and timing, 2 (-vv) = Kafka/configuration debug output
//===----------------------------------------------------------------------===//
#pragma once

namespace fylgje::log {
  /// \brief set once at startup, before any consumer/writer threads exist
  void set_verbosity(int level);
  [[nodiscard]] int verbosity();
  /// \brief -vv and above: gate Kafka/configuration debug prints on this
  [[nodiscard]] bool debug();
}

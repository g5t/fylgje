// Copyright (C) 2026 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief Tests for Calibration JSON serialization
//===----------------------------------------------------------------------===//
#include <gtest/gtest.h>
#include "Calibration.h"

namespace {

TEST(CalibrationJsonTest, RoundTrip) {
  Calibration calibration;
  calibration.setVersion(1);
  calibration.setDate(std::string{"2026-10-26T12:00:00"});
  calibration.setInfo("round trip");
  calibration.setInstrument("bifrost");
  calibration.setPulseHeightUpper(20000);
  calibration.setGroups({CalibrationGroup(0, {CalibrationUnit(0, {0.02, 0.3}, {0, 0, 0, 0}),
                                              CalibrationUnit(1, {0.35, 0.64}, {0.1, 0, 0, 0}),
                                              CalibrationUnit(2, {0.98, 0.7}, {0, 0, 0, 0.2})})});

  const nlohmann::json json = calibration;
  // the key the EFU's CDCalibration reads
  ASSERT_TRUE(json["Calibration"].contains("Parameters"));

  const auto back = json.get<Calibration>();
  EXPECT_EQ(back.version(), 1);
  EXPECT_EQ(back.dateString(), calibration.dateString());
  EXPECT_EQ(back.info(), "round trip");
  EXPECT_EQ(back.instrument(), "bifrost");
  EXPECT_EQ(back.pulseHeightUpper(), 20000);
  ASSERT_EQ(back.groupCount(), 1u);
  ASSERT_EQ(back.elementCount(), 3u);
  const auto & unit = back.groups()[0].elements[2];
  EXPECT_EQ(unit.left, 0.98);
  EXPECT_EQ(unit.right, 0.7);
  EXPECT_EQ(unit.c3.value_or(0), 0.2);
}

} // namespace

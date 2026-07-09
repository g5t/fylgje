// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief fylgje interface to assign pixel IDs like the EFU
//===----------------------------------------------------------------------===//
#pragma once
#include <map>
#include <vector>
#include <fmt/format.h>
#include <h5cpp/hdf5.hpp>
#include "Calibration.h"
#include "Message.h"

namespace bifrost::data {
  ///\brief Manager for holding and updating detector pixel data for BIFROST-fylgje
  class PixelManager {
  public:
    using data_t = std::vector<int>;
  private:
    ///\param pixel_data data points to store post-EFU-calculation results
    data_t pixel_data;

    int arcs;
    int triplets;

    ///\param tubes_per_triplet The number of tubes in each triplet (3)
    int tubes_per_triplet;
    ///\param pixels_per_tube The number of pixels in each tube (100)
    int pixels_per_tube;
    ///\param pixels_per_arc The number of pixels in each (triplet-)arc (2700)
    int pixels_per_arc;
    ///\param pixels_per_tube_arc The number of pixels in each tube-arc (900)
    int pixels_per_tube_arc;
    ///\param total_pixels The total number of pixels in the detector (13500)
    int total_pixels;

    ///\param calibration The calibration object to use for filtering data
    Calibration & calibration;

  public:
    PixelManager(int arcs, int triplets, int tubes, int pixels, Calibration & calib)
        : arcs{arcs}, triplets{triplets}, tubes_per_triplet{tubes}, pixels_per_tube{pixels}, calibration(calib)
    {
      pixels_per_tube_arc = triplets * pixels_per_tube;
      pixels_per_arc = tubes_per_triplet * pixels_per_tube_arc;
      total_pixels = pixels_per_arc * arcs;
      pixel_data.resize(total_pixels, 0);
    }

    ///\brief Replicate the EFU calculations to identify a unique pixel number
    ///\returns 0 if no valid pixel
    [[nodiscard]] int pixel(int arc, int triplet, int a, int b) const;

    ///\brief Reset all histogram data to zeros
    void clear(){
      std::fill(pixel_data.begin(), pixel_data.end(), 0);
    }

    ///\brief Read-only access to the raw pixel count vector
    const data_t & counts() const { return pixel_data; }

    ///\brief Number of arcs
    int num_arcs() const { return arcs; }
    ///\brief Number of triplets per arc
    int num_triplets() const { return triplets; }
    ///\brief Number of tubes per triplet
    int num_tubes() const { return tubes_per_triplet; }
    ///\brief Number of pixels per tube
    int num_pixels() const { return pixels_per_tube; }
    ///\brief Maximum count across all pixels
    int max_count() const { return *std::max_element(pixel_data.begin(), pixel_data.end()); }

    ///\brief Add a new data point to the appropriate pixel
    bool add(int arc, int triplet, int a, int b);

    ///\brief Determine if the charge division would give a pixel number
    bool includes(int arc, int triplet, int a, int b) const;

    void save_to(const std::filesystem::path & file, const std::optional<std::string> & group = std::nullopt) const;
    void save_to(const hdf5::file::File & file, const std::optional<std::string> & group = std::nullopt) const;
    void save_to(const hdf5::node::Group & group) const;
  private:
    ///\brief Calculate the group number from the arc and triplet numbers
    ///\param arc The arc number
    ///\param triplet The triplet number
    ///\note This is an implementation detail that must match the Calibration data, and is therefore not public
    [[nodiscard]] int group(int arc, int triplet) const;
  };
}
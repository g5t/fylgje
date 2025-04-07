// Copyright (C) 2025 European Spallation Source, ERIC. See LICENSE file
//===----------------------------------------------------------------------===//
///
/// \file
///
/// \brief details needed for fylgje BIFROST to understand AR51 messages
//===----------------------------------------------------------------------===//
#pragma once
#include <map>
#include <vector>
#include <fmt/format.h>
#include <h5cpp/hdf5.hpp>

namespace bifrost {
  struct message {
    int fiber;
    int group;
    int a;
    int b;
    double time;
    uint32_t high;
    uint32_t low;
  };
  typedef struct message message_t;

  hdf5::datatype::Compound message_type();

  /// \brief Convert from group number to arc number
  /// The group number is the index of a triplet within a fiber-ring which is in the range (0, 15]
  /// The arc number corresponds to the triplet energy, which has five discrete values
  inline int arc(int group) {
    return group / 3;
  }

  /// \brief Convert from fiber-ring number to module number
  /// The fiber-ring number is the index of a fiber-ring within a module which is in the range (0, 5)
  /// Each two successive fiber numbers correspond to a single ring, or module.
  inline int module(int fiber) {
    return fiber / 2;
  }

  /// \brief Convert from fiber-ring and group number to triplet number
  /// The triplet number indexes all triplets of a single energy from smallest to largest scattering angle.
  /// Module 0 holds triplets (0,1,2); module 1 holds triplets (3,4,5); and module 2 holds triplets (6,7,8).
  /// Within a module the group number steps through triplet types (short, medium, long) in order, and
  /// triplet energies [2.7, 3.2, 3.8, 4.4, 5.0] meV in order.
  inline int triplet(int fiber, int group) {
    int type = group % 3;
    return module(fiber) * 3 + type;
  }

}

/// \brief Specialization of the h5cpp datatype trait for bifrost::message_t
namespace hdf5::datatype {
  template<>
  class TypeTrait<bifrost::message_t>
  {
  public:
    using Type = bifrost::message_t;
    using TypeClass = Compound;

    static TypeClass create(const Type& = Type())
    {
      return bifrost::message_type();
    }

    const static TypeClass & get(const Type & = Type()) {
      const static TypeClass & cref_ = create();
      return cref_;
    }
  };
}

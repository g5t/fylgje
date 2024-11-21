#pragma once

#include <array>

namespace fylgje {
  template <std::size_t N>
  class Cycles {
  public:
    using array_t = std::array<int, N>;
    explicit Cycles(array_t const& lengths) : lengths(lengths) {}
    explicit Cycles(array_t&& lengths) : lengths(std::move(lengths)) {}
    Cycles(array_t const & lengths, array_t const & current) : lengths(lengths), current(current) {}

    bool next() {
      for (int i = N; i-- > 0;) {
        if (++current[i] < lengths[i]) {
          return true;
        }
        current[i] = 0;
      }
      return false;
    }

    array_t const& operator*() const {
      return current;
    }
    int operator[](int i) const {
      return current[i];
    }

    void set(array_t const & c) {
      this->current = c;
    }

    int at(int i) {
      if (i < 0 || i >= static_cast<int>(N)) {
        throw std::out_of_range("The index is out of range");
      }
      return current[i];
    }

  private:
    array_t lengths;
    array_t current{};
  };

}
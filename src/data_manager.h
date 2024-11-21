#pragma once
#include <map>
#include <vector>
#include <QVector>
#include <QPlot/qcustomplot/qcustomplot.h>
#include <fmt/format.h>

namespace bifrost {
    inline int arc(int group) {
        return group / 3;
    }

    inline int module(int fiber) {
        return fiber / 2;
    }

    inline int triplet(int fiber, int group) {
        int type = group % 3;
        return module(fiber) * 3 + type;
    }
}

namespace bifrost::data {
    int hist_a_or_b(int x, int shift, int bins);
    int hist_p(int x, int shift, int bins);
    int hist_t(double x, int bins);
    int hist_x(int a, int b, int bins);

    template<class T, class R>
    void iota_step(T begin, T end, R step, R first=R(0)){
        *begin = first;
        for (auto p=begin; p != end; ++p){
            auto n = p+1;
            if (n != end) *n = *p + step;
        }
    }

    enum class Type {unknown=-1, x=0, a=1, p=2, xp=3, ab=4, b=5, xt=6, pt=7, t=8};

    constexpr Type TYPE1D[]{Type::a, Type::b, Type::x, Type::p, Type::t};
    constexpr Type TYPE2D[]{Type::xp, Type::ab, Type::xt, Type::pt};
    constexpr Type TYPEND[]{Type::a, Type::b, Type::x, Type::p, Type::t, Type::xp, Type::ab, Type::xt, Type::pt};
    constexpr size_t TYPECOUNT{9}; // just those used for indexing map_t

    using key_t = int64_t;

    // This replaces an earlier attempt to use std::map for the data container
    // doing so caused problems with indexing/reindexing intensity limits in the window
    template<class T> class map_t : public std::vector<T> {
    public:
      [[nodiscard]] size_t count(key_t key) const {
        if (key < 0){
          fmt::print("Negative key? {}\n", key);
          return 0;
        }
        if (static_cast<size_t>(key) >= this->size()){
          fmt::print("Too big key since {} > {}\n", key, this->size());
          return 0;
        }
        return 1;
      }
    };

    bool is_1D(Type t);
    bool is_2D(Type t);

    constexpr int SHIFT1D = 5;
    constexpr int BIN1D = (1 << 15) >> SHIFT1D;
    constexpr int SHIFT2D = 6;
    constexpr int BIN2D = (1 << 15) >> SHIFT2D;

    class Manager{
    public:
      using AX = std::vector<double>;
      using D1 = std::vector<double>;
      using D2 = QCPColorMapData;
      using data_t = std::vector<int>;
    private:
      map_t<data_t> data;

      std::map<Type, int> bins_1d {{Type::a, BIN1D}, {Type::b, BIN1D}, {Type::p, BIN1D}, {Type::x, BIN1D}, {Type::t, BIN1D}};
      std::map<Type, int> bins_2d {{Type::a, BIN2D}, {Type::b, BIN2D}, {Type::x, BIN2D}, {Type::p, BIN2D}, {Type::t, BIN2D}};

      int arcs;
      int triplets;

    public:
      Manager(int arcs, int triplets): arcs(arcs), triplets(triplets) {
        // setup data objects ...
        data.resize(key_count());
        for (int a = 0; a<arcs; ++a){
          for (int t = 0; t<triplets; ++t) {
            for (auto k: TYPE1D) data[key(a, t, k)].resize(BIN1D, 0);
            for (auto k: TYPE2D) data[key(a, t, k)].resize(BIN2D*BIN2D, 0);
          }
        }
      }
      ~Manager() = default;

      void clear(){
          //for (auto & [k, x]: data) std::fill(x.begin(), x.end(), 0);
          for (auto & d: data) std::fill(d.begin(), d.end(), 0);
      }
      bool add(int arc, int triplet, int a, int b, double time);

      [[nodiscard]] double max() const;
      [[nodiscard]] double max(int arc) const;
      [[nodiscard]] double max(int arc, int triplet) const;
      [[nodiscard]] double max(int arc, Type t) const;
      [[nodiscard]] double max(int arc, int triplet, Type t) const;
      [[nodiscard]] double max(key_t k) const;

      [[nodiscard]] double min() const;
      [[nodiscard]] double min(int arc) const;
      [[nodiscard]] double min(int arc, int triplet) const;
      [[nodiscard]] double min(int arc, Type t) const;
      [[nodiscard]] double min(int arc, int triplet, Type t) const;

      [[nodiscard]] D1 data_1D(int arc, int triplet, Type t) const;
      [[nodiscard]] D1 data_1D(key_t k) const;
      [[nodiscard]] D2 * data_2D(int arc, int triplet, Type t) const;
      [[nodiscard]] D2 * data_2D(key_t k) const;

      [[nodiscard]] AX axis(Type t) const {
        int bins{0};
        if (is_1D(t)){
          if (bins_1d.count(t)) bins = bins_1d.at(t);
        } else {
          if (bins_2d.count(t)) bins = bins_2d.at(t);
        }
        AX x(bins);
        double range{1<<15}, start{0};
        if (Type::x == t){
          range = 2;
          start = -1;
        } else if (Type::p == t){
          range *= 2;
        } else if (Type::t == t){
          range = 1.0 / 14.0;
          start = 1.0 / 14.0 / 2.0 / (bins + 1);
        }
        iota_step(x.begin(), x.end(), range / (bins + 1), start);
        return x;
      }

      void set_bins_1d(Type t, int m){
        if (m > 0 && m <= BIN1D) bins_1d[t] = m;
      }
      void set_bins_2d(Type t, int m){
        if (m > 0 && m <= BIN2D) bins_2d[t] = m;
      }

      [[nodiscard]] key_t key(int arc, int triplet, Type t) const {
        if (arc < 0 || arc >= arcs){
          fmt::print("arc must be in (0, {}), given {}", arcs-1, arc);
          throw std::runtime_error(fmt::format("arc must be in (0, {}), given {}", arcs-1, arc));
        }
        if (triplet < 0 || triplet >= triplets) {
          fmt::print("triplet must be in (0, {}), given {}", triplets-1, triplet);
          throw std::runtime_error(fmt::format("triplet must be in (0, {}), given {}", triplets-1, triplet));
        }
        return static_cast<key_t>(t) * (arcs * triplets) + triplet * arcs + arc;
      }
      [[nodiscard]] Type key_type(key_t k) const {
        return static_cast<Type>(k / (arcs * triplets));
      }
      [[nodiscard]] int key_triplet(key_t k) const {
        return static_cast<int>((k % (arcs * triplets)) / arcs);
      }
      [[nodiscard]] int key_arc(key_t k) const {
        return static_cast<int>(k % arcs);
      }
      [[nodiscard]] key_t key_count() const {
        return TYPECOUNT * arcs * triplets;
      }

    private:
      bool add_1D(int arc, int triplet, int a, int b, double time);
      bool add_2D(int arc, int triplet, int a, int b, double time);

      [[nodiscard]] int max_1D(int arc, int triplet, Type t) const;
      [[nodiscard]] int max_1D(key_t t) const;
      [[nodiscard]] int max_2D(int arc, int triplet, Type t) const;
      [[nodiscard]] int max_2D(key_t t) const;
      [[nodiscard]] int min_1D(int arc, int triplet, Type t) const;
      [[nodiscard]] int min_2D(int arc, int triplet, Type t) const;

      [[nodiscard]] std::pair<int, int> bins_2D(Type t) const{
        Type x{Type::unknown}, y;
        if (Type::ab == t){
          x = Type::a;
          y = Type::b;
        } else if (Type::pt == t){
          x = Type::p;
          y = Type::t;
        } else if (Type::xt == t){
          x = Type::x;
          y = Type::t;
        } else if (Type::xp == t){
          x = Type::x;
          y = Type::p;
        }
        if (x == Type::unknown) return std::make_pair(-1, -1);
        if (!bins_2d.count(x) || !bins_2d.count(y)) return std::make_pair(0, 0);
        return std::make_pair(bins_2d.at(x), bins_2d.at(y));
      }

    };
}

std::ostream & operator<<(std::ostream & os, ::bifrost::data::Type type);
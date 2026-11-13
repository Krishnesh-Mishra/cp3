#ifndef GIANT_MID_M84_HPP
#define GIANT_MID_M84_HPP

#include "../base/b124.hpp"
#include "../base/b36.hpp"

inline int m84(int x) {
    return b124(x) + b36(x + 1);
}

#endif  // GIANT_MID_M84_HPP

#ifndef GIANT_MID_M13_HPP
#define GIANT_MID_M13_HPP

#include "../base/b39.hpp"
#include "../base/b79.hpp"

inline int m13(int x) {
    return b39(x) + b79(x + 1);
}

#endif  // GIANT_MID_M13_HPP

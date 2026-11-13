#ifndef GIANT_MID_M40_HPP
#define GIANT_MID_M40_HPP

#include "../base/b120.hpp"
#include "../base/b32.hpp"

inline int m40(int x) {
    return b120(x) + b32(x + 1);
}

#endif  // GIANT_MID_M40_HPP

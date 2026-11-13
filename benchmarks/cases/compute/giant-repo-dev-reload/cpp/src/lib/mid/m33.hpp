#ifndef GIANT_MID_M33_HPP
#define GIANT_MID_M33_HPP

#include "../base/b99.hpp"
#include "../base/b11.hpp"

inline int m33(int x) {
    return b99(x) + b11(x + 1);
}

#endif  // GIANT_MID_M33_HPP

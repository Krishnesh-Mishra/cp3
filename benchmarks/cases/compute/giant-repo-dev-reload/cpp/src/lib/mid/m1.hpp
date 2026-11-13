#ifndef GIANT_MID_M1_HPP
#define GIANT_MID_M1_HPP

#include "../base/b3.hpp"
#include "../base/b43.hpp"

inline int m1(int x) {
    return b3(x) + b43(x + 1);
}

#endif  // GIANT_MID_M1_HPP

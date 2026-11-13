#ifndef GIANT_MID_M0_HPP
#define GIANT_MID_M0_HPP

#include "../base/b0.hpp"
#include "../base/b40.hpp"

inline int m0(int x) {
    return b0(x) + b40(x + 1);
}

#endif  // GIANT_MID_M0_HPP

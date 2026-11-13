#ifndef GIANT_MID_M89_HPP
#define GIANT_MID_M89_HPP

#include "../base/b11.hpp"
#include "../base/b51.hpp"

inline int m89(int x) {
    return b11(x) + b51(x + 1);
}

#endif  // GIANT_MID_M89_HPP

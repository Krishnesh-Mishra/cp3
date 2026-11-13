#ifndef GIANT_MID_M43_HPP
#define GIANT_MID_M43_HPP

#include "../base/b1.hpp"
#include "../base/b41.hpp"

inline int m43(int x) {
    return b1(x) + b41(x + 1);
}

#endif  // GIANT_MID_M43_HPP

#ifndef GIANT_MID_M54_HPP
#define GIANT_MID_M54_HPP

#include "../base/b34.hpp"
#include "../base/b74.hpp"

inline int m54(int x) {
    return b34(x) + b74(x + 1);
}

#endif  // GIANT_MID_M54_HPP

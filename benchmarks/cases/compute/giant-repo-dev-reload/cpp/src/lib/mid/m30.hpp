#ifndef GIANT_MID_M30_HPP
#define GIANT_MID_M30_HPP

#include "../base/b90.hpp"
#include "../base/b2.hpp"

inline int m30(int x) {
    return b90(x) + b2(x + 1);
}

#endif  // GIANT_MID_M30_HPP

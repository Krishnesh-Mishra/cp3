#ifndef GIANT_MID_M16_HPP
#define GIANT_MID_M16_HPP

#include "../base/b48.hpp"
#include "../base/b88.hpp"

inline int m16(int x) {
    return b48(x) + b88(x + 1);
}

#endif  // GIANT_MID_M16_HPP

#ifndef GIANT_MID_M32_HPP
#define GIANT_MID_M32_HPP

#include "../base/b96.hpp"
#include "../base/b8.hpp"

inline int m32(int x) {
    return b96(x) + b8(x + 1);
}

#endif  // GIANT_MID_M32_HPP

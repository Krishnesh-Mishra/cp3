#ifndef GIANT_MID_M24_HPP
#define GIANT_MID_M24_HPP

#include "../base/b72.hpp"
#include "../base/b112.hpp"

inline int m24(int x) {
    return b72(x) + b112(x + 1);
}

#endif  // GIANT_MID_M24_HPP

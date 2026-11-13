#ifndef GIANT_MID_M4_HPP
#define GIANT_MID_M4_HPP

#include "../base/b12.hpp"
#include "../base/b52.hpp"

inline int m4(int x) {
    return b12(x) + b52(x + 1);
}

#endif  // GIANT_MID_M4_HPP

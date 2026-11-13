#ifndef GIANT_MID_M11_HPP
#define GIANT_MID_M11_HPP

#include "../base/b33.hpp"
#include "../base/b73.hpp"

inline int m11(int x) {
    return b33(x) + b73(x + 1);
}

#endif  // GIANT_MID_M11_HPP

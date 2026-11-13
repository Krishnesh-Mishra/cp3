#ifndef GIANT_MID_M44_HPP
#define GIANT_MID_M44_HPP

#include "../base/b4.hpp"
#include "../base/b44.hpp"

inline int m44(int x) {
    return b4(x) + b44(x + 1);
}

#endif  // GIANT_MID_M44_HPP

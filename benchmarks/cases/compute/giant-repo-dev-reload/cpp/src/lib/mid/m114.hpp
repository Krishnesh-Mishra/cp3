#ifndef GIANT_MID_M114_HPP
#define GIANT_MID_M114_HPP

#include "../base/b86.hpp"
#include "../base/b126.hpp"

inline int m114(int x) {
    return b86(x) + b126(x + 1);
}

#endif  // GIANT_MID_M114_HPP

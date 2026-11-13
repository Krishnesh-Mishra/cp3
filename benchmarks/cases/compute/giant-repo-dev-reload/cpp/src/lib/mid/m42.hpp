#ifndef GIANT_MID_M42_HPP
#define GIANT_MID_M42_HPP

#include "../base/b126.hpp"
#include "../base/b38.hpp"

inline int m42(int x) {
    return b126(x) + b38(x + 1);
}

#endif  // GIANT_MID_M42_HPP

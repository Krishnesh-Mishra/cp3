#ifndef GIANT_MID_M48_HPP
#define GIANT_MID_M48_HPP

#include "../base/b16.hpp"
#include "../base/b56.hpp"

inline int m48(int x) {
    return b16(x) + b56(x + 1);
}

#endif  // GIANT_MID_M48_HPP

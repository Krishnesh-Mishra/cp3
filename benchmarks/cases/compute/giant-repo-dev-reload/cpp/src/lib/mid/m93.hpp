#ifndef GIANT_MID_M93_HPP
#define GIANT_MID_M93_HPP

#include "../base/b23.hpp"
#include "../base/b63.hpp"

inline int m93(int x) {
    return b23(x) + b63(x + 1);
}

#endif  // GIANT_MID_M93_HPP

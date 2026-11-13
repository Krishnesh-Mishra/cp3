#ifndef GIANT_MID_M21_HPP
#define GIANT_MID_M21_HPP

#include "../base/b63.hpp"
#include "../base/b103.hpp"

inline int m21(int x) {
    return b63(x) + b103(x + 1);
}

#endif  // GIANT_MID_M21_HPP

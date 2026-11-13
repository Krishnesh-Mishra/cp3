#ifndef GIANT_MID_M102_HPP
#define GIANT_MID_M102_HPP

#include "../base/b50.hpp"
#include "../base/b90.hpp"

inline int m102(int x) {
    return b50(x) + b90(x + 1);
}

#endif  // GIANT_MID_M102_HPP

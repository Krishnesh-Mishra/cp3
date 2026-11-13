#ifndef GIANT_MID_M119_HPP
#define GIANT_MID_M119_HPP

#include "../base/b101.hpp"
#include "../base/b13.hpp"

inline int m119(int x) {
    return b101(x) + b13(x + 1);
}

#endif  // GIANT_MID_M119_HPP

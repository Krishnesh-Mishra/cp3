#ifndef GIANT_MID_M115_HPP
#define GIANT_MID_M115_HPP

#include "../base/b89.hpp"
#include "../base/b1.hpp"

inline int m115(int x) {
    return b89(x) + b1(x + 1);
}

#endif  // GIANT_MID_M115_HPP

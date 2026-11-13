#ifndef GIANT_MID_M49_HPP
#define GIANT_MID_M49_HPP

#include "../base/b19.hpp"
#include "../base/b59.hpp"

inline int m49(int x) {
    return b19(x) + b59(x + 1);
}

#endif  // GIANT_MID_M49_HPP

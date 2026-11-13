#ifndef GIANT_MID_M14_HPP
#define GIANT_MID_M14_HPP

#include "../base/b42.hpp"
#include "../base/b82.hpp"

inline int m14(int x) {
    return b42(x) + b82(x + 1);
}

#endif  // GIANT_MID_M14_HPP

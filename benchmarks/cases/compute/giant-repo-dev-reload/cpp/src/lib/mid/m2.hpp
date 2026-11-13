#ifndef GIANT_MID_M2_HPP
#define GIANT_MID_M2_HPP

#include "../base/b6.hpp"
#include "../base/b46.hpp"

inline int m2(int x) {
    return b6(x) + b46(x + 1);
}

#endif  // GIANT_MID_M2_HPP

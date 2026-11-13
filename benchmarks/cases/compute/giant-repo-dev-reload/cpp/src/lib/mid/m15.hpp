#ifndef GIANT_MID_M15_HPP
#define GIANT_MID_M15_HPP

#include "../base/b45.hpp"
#include "../base/b85.hpp"

inline int m15(int x) {
    return b45(x) + b85(x + 1);
}

#endif  // GIANT_MID_M15_HPP

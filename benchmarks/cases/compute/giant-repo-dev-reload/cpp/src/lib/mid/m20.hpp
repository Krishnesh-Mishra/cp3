#ifndef GIANT_MID_M20_HPP
#define GIANT_MID_M20_HPP

#include "../base/b60.hpp"
#include "../base/b100.hpp"

inline int m20(int x) {
    return b60(x) + b100(x + 1);
}

#endif  // GIANT_MID_M20_HPP

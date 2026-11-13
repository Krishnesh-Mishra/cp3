#ifndef GIANT_MID_M10_HPP
#define GIANT_MID_M10_HPP

#include "../base/b30.hpp"
#include "../base/b70.hpp"

inline int m10(int x) {
    return b30(x) + b70(x + 1);
}

#endif  // GIANT_MID_M10_HPP

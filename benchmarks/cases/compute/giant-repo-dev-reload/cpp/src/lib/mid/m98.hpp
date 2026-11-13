#ifndef GIANT_MID_M98_HPP
#define GIANT_MID_M98_HPP

#include "../base/b38.hpp"
#include "../base/b78.hpp"

inline int m98(int x) {
    return b38(x) + b78(x + 1);
}

#endif  // GIANT_MID_M98_HPP

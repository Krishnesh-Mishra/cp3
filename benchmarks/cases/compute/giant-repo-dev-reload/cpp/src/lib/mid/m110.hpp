#ifndef GIANT_MID_M110_HPP
#define GIANT_MID_M110_HPP

#include "../base/b74.hpp"
#include "../base/b114.hpp"

inline int m110(int x) {
    return b74(x) + b114(x + 1);
}

#endif  // GIANT_MID_M110_HPP

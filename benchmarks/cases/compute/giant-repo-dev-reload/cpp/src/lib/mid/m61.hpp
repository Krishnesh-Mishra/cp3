#ifndef GIANT_MID_M61_HPP
#define GIANT_MID_M61_HPP

#include "../base/b55.hpp"
#include "../base/b95.hpp"

inline int m61(int x) {
    return b55(x) + b95(x + 1);
}

#endif  // GIANT_MID_M61_HPP

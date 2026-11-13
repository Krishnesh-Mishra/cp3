#ifndef GIANT_MID_M100_HPP
#define GIANT_MID_M100_HPP

#include "../base/b44.hpp"
#include "../base/b84.hpp"

inline int m100(int x) {
    return b44(x) + b84(x + 1);
}

#endif  // GIANT_MID_M100_HPP

#ifndef GIANT_MID_M112_HPP
#define GIANT_MID_M112_HPP

#include "../base/b80.hpp"
#include "../base/b120.hpp"

inline int m112(int x) {
    return b80(x) + b120(x + 1);
}

#endif  // GIANT_MID_M112_HPP

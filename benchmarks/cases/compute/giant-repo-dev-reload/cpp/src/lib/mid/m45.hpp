#ifndef GIANT_MID_M45_HPP
#define GIANT_MID_M45_HPP

#include "../base/b7.hpp"
#include "../base/b47.hpp"

inline int m45(int x) {
    return b7(x) + b47(x + 1);
}

#endif  // GIANT_MID_M45_HPP

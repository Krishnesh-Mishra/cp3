#ifndef GIANT_MID_M3_HPP
#define GIANT_MID_M3_HPP

#include "../base/b9.hpp"
#include "../base/b49.hpp"

inline int m3(int x) {
    return b9(x) + b49(x + 1);
}

#endif  // GIANT_MID_M3_HPP

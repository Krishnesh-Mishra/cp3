#ifndef GIANT_MID_M5_HPP
#define GIANT_MID_M5_HPP

#include "../base/b15.hpp"
#include "../base/b55.hpp"

inline int m5(int x) {
    return b15(x) + b55(x + 1);
}

#endif  // GIANT_MID_M5_HPP

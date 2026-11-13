#ifndef GIANT_MID_M104_HPP
#define GIANT_MID_M104_HPP

#include "../base/b56.hpp"
#include "../base/b96.hpp"

inline int m104(int x) {
    return b56(x) + b96(x + 1);
}

#endif  // GIANT_MID_M104_HPP

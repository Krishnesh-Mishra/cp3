#ifndef GIANT_MID_M76_HPP
#define GIANT_MID_M76_HPP

#include "../base/b100.hpp"
#include "../base/b12.hpp"

inline int m76(int x) {
    return b100(x) + b12(x + 1);
}

#endif  // GIANT_MID_M76_HPP

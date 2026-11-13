#ifndef GIANT_MID_M56_HPP
#define GIANT_MID_M56_HPP

#include "../base/b40.hpp"
#include "../base/b80.hpp"

inline int m56(int x) {
    return b40(x) + b80(x + 1);
}

#endif  // GIANT_MID_M56_HPP

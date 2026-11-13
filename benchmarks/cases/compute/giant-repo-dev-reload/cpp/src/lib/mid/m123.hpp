#ifndef GIANT_MID_M123_HPP
#define GIANT_MID_M123_HPP

#include "../base/b113.hpp"
#include "../base/b25.hpp"

inline int m123(int x) {
    return b113(x) + b25(x + 1);
}

#endif  // GIANT_MID_M123_HPP

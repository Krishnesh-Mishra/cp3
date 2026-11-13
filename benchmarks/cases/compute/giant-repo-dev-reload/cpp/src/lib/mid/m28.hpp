#ifndef GIANT_MID_M28_HPP
#define GIANT_MID_M28_HPP

#include "../base/b84.hpp"
#include "../base/b124.hpp"

inline int m28(int x) {
    return b84(x) + b124(x + 1);
}

#endif  // GIANT_MID_M28_HPP

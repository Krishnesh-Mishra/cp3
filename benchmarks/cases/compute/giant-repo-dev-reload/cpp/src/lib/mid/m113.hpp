#ifndef GIANT_MID_M113_HPP
#define GIANT_MID_M113_HPP

#include "../base/b83.hpp"
#include "../base/b123.hpp"

inline int m113(int x) {
    return b83(x) + b123(x + 1);
}

#endif  // GIANT_MID_M113_HPP

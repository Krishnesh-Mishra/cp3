#ifndef GIANT_MID_M22_HPP
#define GIANT_MID_M22_HPP

#include "../base/b66.hpp"
#include "../base/b106.hpp"

inline int m22(int x) {
    return b66(x) + b106(x + 1);
}

#endif  // GIANT_MID_M22_HPP

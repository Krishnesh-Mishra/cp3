#ifndef GIANT_MID_M88_HPP
#define GIANT_MID_M88_HPP

#include "../base/b8.hpp"
#include "../base/b48.hpp"

inline int m88(int x) {
    return b8(x) + b48(x + 1);
}

#endif  // GIANT_MID_M88_HPP

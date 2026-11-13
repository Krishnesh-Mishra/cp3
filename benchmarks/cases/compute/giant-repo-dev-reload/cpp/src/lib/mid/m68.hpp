#ifndef GIANT_MID_M68_HPP
#define GIANT_MID_M68_HPP

#include "../base/b76.hpp"
#include "../base/b116.hpp"

inline int m68(int x) {
    return b76(x) + b116(x + 1);
}

#endif  // GIANT_MID_M68_HPP

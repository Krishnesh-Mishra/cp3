#ifndef GIANT_MID_M58_HPP
#define GIANT_MID_M58_HPP

#include "../base/b46.hpp"
#include "../base/b86.hpp"

inline int m58(int x) {
    return b46(x) + b86(x + 1);
}

#endif  // GIANT_MID_M58_HPP

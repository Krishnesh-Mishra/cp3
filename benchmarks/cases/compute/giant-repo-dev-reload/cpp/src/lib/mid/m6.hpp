#ifndef GIANT_MID_M6_HPP
#define GIANT_MID_M6_HPP

#include "../base/b18.hpp"
#include "../base/b58.hpp"

inline int m6(int x) {
    return b18(x) + b58(x + 1);
}

#endif  // GIANT_MID_M6_HPP

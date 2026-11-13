#ifndef GIANT_MID_M47_HPP
#define GIANT_MID_M47_HPP

#include "../base/b13.hpp"
#include "../base/b53.hpp"

inline int m47(int x) {
    return b13(x) + b53(x + 1);
}

#endif  // GIANT_MID_M47_HPP

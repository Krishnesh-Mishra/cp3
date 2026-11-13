#ifndef GIANT_MID_M25_HPP
#define GIANT_MID_M25_HPP

#include "../base/b75.hpp"
#include "../base/b115.hpp"

inline int m25(int x) {
    return b75(x) + b115(x + 1);
}

#endif  // GIANT_MID_M25_HPP

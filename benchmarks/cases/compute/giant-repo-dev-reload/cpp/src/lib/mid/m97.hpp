#ifndef GIANT_MID_M97_HPP
#define GIANT_MID_M97_HPP

#include "../base/b35.hpp"
#include "../base/b75.hpp"

inline int m97(int x) {
    return b35(x) + b75(x + 1);
}

#endif  // GIANT_MID_M97_HPP

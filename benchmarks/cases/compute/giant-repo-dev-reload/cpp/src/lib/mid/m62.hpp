#ifndef GIANT_MID_M62_HPP
#define GIANT_MID_M62_HPP

#include "../base/b58.hpp"
#include "../base/b98.hpp"

inline int m62(int x) {
    return b58(x) + b98(x + 1);
}

#endif  // GIANT_MID_M62_HPP

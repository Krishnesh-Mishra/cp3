#ifndef GIANT_MID_M94_HPP
#define GIANT_MID_M94_HPP

#include "../base/b26.hpp"
#include "../base/b66.hpp"

inline int m94(int x) {
    return b26(x) + b66(x + 1);
}

#endif  // GIANT_MID_M94_HPP

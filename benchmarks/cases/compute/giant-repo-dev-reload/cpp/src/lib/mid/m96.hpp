#ifndef GIANT_MID_M96_HPP
#define GIANT_MID_M96_HPP

#include "../base/b32.hpp"
#include "../base/b72.hpp"

inline int m96(int x) {
    return b32(x) + b72(x + 1);
}

#endif  // GIANT_MID_M96_HPP

#ifndef GIANT_MID_M65_HPP
#define GIANT_MID_M65_HPP

#include "../base/b67.hpp"
#include "../base/b107.hpp"

inline int m65(int x) {
    return b67(x) + b107(x + 1);
}

#endif  // GIANT_MID_M65_HPP

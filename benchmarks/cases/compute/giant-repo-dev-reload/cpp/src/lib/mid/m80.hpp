#ifndef GIANT_MID_M80_HPP
#define GIANT_MID_M80_HPP

#include "../base/b112.hpp"
#include "../base/b24.hpp"

inline int m80(int x) {
    return b112(x) + b24(x + 1);
}

#endif  // GIANT_MID_M80_HPP

#ifndef GIANT_MID_M34_HPP
#define GIANT_MID_M34_HPP

#include "../base/b102.hpp"
#include "../base/b14.hpp"

inline int m34(int x) {
    return b102(x) + b14(x + 1);
}

#endif  // GIANT_MID_M34_HPP

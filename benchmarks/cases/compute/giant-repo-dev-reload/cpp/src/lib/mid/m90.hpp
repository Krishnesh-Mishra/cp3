#ifndef GIANT_MID_M90_HPP
#define GIANT_MID_M90_HPP

#include "../base/b14.hpp"
#include "../base/b54.hpp"

inline int m90(int x) {
    return b14(x) + b54(x + 1);
}

#endif  // GIANT_MID_M90_HPP

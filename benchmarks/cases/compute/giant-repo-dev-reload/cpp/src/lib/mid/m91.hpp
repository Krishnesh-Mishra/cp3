#ifndef GIANT_MID_M91_HPP
#define GIANT_MID_M91_HPP

#include "../base/b17.hpp"
#include "../base/b57.hpp"

inline int m91(int x) {
    return b17(x) + b57(x + 1);
}

#endif  // GIANT_MID_M91_HPP

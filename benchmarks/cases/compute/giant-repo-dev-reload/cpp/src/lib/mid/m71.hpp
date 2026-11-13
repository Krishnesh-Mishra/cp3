#ifndef GIANT_MID_M71_HPP
#define GIANT_MID_M71_HPP

#include "../base/b85.hpp"
#include "../base/b125.hpp"

inline int m71(int x) {
    return b85(x) + b125(x + 1);
}

#endif  // GIANT_MID_M71_HPP

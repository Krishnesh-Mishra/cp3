#ifndef GIANT_MID_M55_HPP
#define GIANT_MID_M55_HPP

#include "../base/b37.hpp"
#include "../base/b77.hpp"

inline int m55(int x) {
    return b37(x) + b77(x + 1);
}

#endif  // GIANT_MID_M55_HPP

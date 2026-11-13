#ifndef GIANT_MID_M109_HPP
#define GIANT_MID_M109_HPP

#include "../base/b71.hpp"
#include "../base/b111.hpp"

inline int m109(int x) {
    return b71(x) + b111(x + 1);
}

#endif  // GIANT_MID_M109_HPP

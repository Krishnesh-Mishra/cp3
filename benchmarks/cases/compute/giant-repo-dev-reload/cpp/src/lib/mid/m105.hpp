#ifndef GIANT_MID_M105_HPP
#define GIANT_MID_M105_HPP

#include "../base/b59.hpp"
#include "../base/b99.hpp"

inline int m105(int x) {
    return b59(x) + b99(x + 1);
}

#endif  // GIANT_MID_M105_HPP

#ifndef GIANT_MID_M60_HPP
#define GIANT_MID_M60_HPP

#include "../base/b52.hpp"
#include "../base/b92.hpp"

inline int m60(int x) {
    return b52(x) + b92(x + 1);
}

#endif  // GIANT_MID_M60_HPP

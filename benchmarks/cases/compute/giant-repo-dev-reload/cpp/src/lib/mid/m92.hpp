#ifndef GIANT_MID_M92_HPP
#define GIANT_MID_M92_HPP

#include "../base/b20.hpp"
#include "../base/b60.hpp"

inline int m92(int x) {
    return b20(x) + b60(x + 1);
}

#endif  // GIANT_MID_M92_HPP

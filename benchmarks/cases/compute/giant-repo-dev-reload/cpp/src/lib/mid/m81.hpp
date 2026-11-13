#ifndef GIANT_MID_M81_HPP
#define GIANT_MID_M81_HPP

#include "../base/b115.hpp"
#include "../base/b27.hpp"

inline int m81(int x) {
    return b115(x) + b27(x + 1);
}

#endif  // GIANT_MID_M81_HPP
